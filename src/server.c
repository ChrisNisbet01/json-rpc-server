#include "server.h"

#include "easy_pc/easy_pc_ast.h"

#include "json.h"
#include "json_ast.h"
#include "json_ast_actions.h"
#include "utils.h"

#include <errno.h>
#include <libubox/list.h>
#include <libubox/runqueue.h>
#include <libubox/uloop.h>
#include <libubus.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

typedef struct write_queue_entry_st
{
    struct list_head list;
    char * buf;
    size_t len;
    size_t pos;
} write_queue_entry_st;

static void
check_exit_condition(rpc_server_st * svr)
{
    if (svr->eof_reached && list_empty(&svr->write_queue) && list_empty(&svr->tool_queue.tasks_active.list))
    {
        uloop_end();
    }
}

static void
write_queue_cb(struct uloop_fd * u, unsigned int events)
{
    UNUSED_PARAM(events);
    rpc_server_st * const svr = container_of(u, rpc_server_st, out_uloop_fd);

    while (!list_empty(&svr->write_queue))
    {
        write_queue_entry_st * entry = list_first_entry(&svr->write_queue, write_queue_entry_st, list);

        ssize_t const bytes_written = write(u->fd, entry->buf + entry->pos, entry->len - entry->pos);

        if (bytes_written < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                /* Output buffer full, wait for next ULOOP_WRITE event. */
                return;
            }
            else if (errno == EINTR)
            {
                continue;
            }
            else
            {
                perror("write_queue_cb: write failed");
                uloop_end();
                return;
            }
        }

        entry->pos += bytes_written;
        if (entry->pos == entry->len)
        {
            list_del(&entry->list);
            free(entry->buf);
            free(entry);
        }
    }

    /* Queue is empty, stop monitoring for writability. */
    uloop_fd_delete(&svr->out_uloop_fd);
    check_exit_condition(svr);
}

void
rpc_server_queue_response(rpc_server_st * svr, struct json_object * res)
{
    struct json_object * id = NULL;
    json_object_object_get_ex(res, "id", &id);
    fprintf(stderr, "DEBUG: Queuing response (id: %s)\n", id ? json_object_get_string(id) : "null");

    char const * json_str = json_object_to_json_string_ext(res, JSON_C_TO_STRING_PLAIN);
    size_t const len = strlen(json_str);

    write_queue_entry_st * entry = malloc(sizeof(write_queue_entry_st));
    /* We add a newline to each response as per JSON-RPC over pipes convention. */
    entry->len = len + 1;
    entry->buf = malloc(entry->len);
    memcpy(entry->buf, json_str, len);
    entry->buf[len] = '\n';
    entry->pos = 0;

    bool const was_empty = list_empty(&svr->write_queue);
    list_add_tail(&entry->list, &svr->write_queue);

    if (was_empty)
    {
        uloop_fd_add(&svr->out_uloop_fd, ULOOP_WRITE);
        /* Try to write immediately. */
        write_queue_cb(&svr->out_uloop_fd, ULOOP_WRITE);
    }
}

void
rpc_server_register_method(rpc_server_st * svr, char const * name, rpc_handler_fn handler)
{
    if (svr->registry.count == svr->registry.capacity)
    {
        svr->registry.capacity = svr->registry.capacity == 0 ? 8 : svr->registry.capacity * 2;
        svr->registry.methods = realloc(svr->registry.methods, svr->registry.capacity * sizeof(rpc_method_st));
    }
    svr->registry.methods[svr->registry.count].name = strdup(name);
    svr->registry.methods[svr->registry.count].handler = handler;
    svr->registry.count++;
}

static void
queue_error_response(rpc_server_st * svr, struct json_object * id, int code, char const * message)
{
    struct json_object * res = json_object_new_object();
    json_object_object_add(res, "jsonrpc", json_object_new_string("2.0"));

    struct json_object * error = json_object_new_object();
    json_object_object_add(error, "code", json_object_new_int(code));
    json_object_object_add(error, "message", json_object_new_string(message));
    json_object_object_add(res, "error", error);

    if (id)
    {
        json_object_object_add(res, "id", json_object_get(id));
    }
    else
    {
        json_object_object_add(res, "id", NULL);
    }

    rpc_server_queue_response(svr, res);
    json_object_put(res);
}

static void
handle_rpc_message(rpc_server_st * svr, struct json_object * msg)
{
    struct json_object * id = NULL;
    struct json_object * method_obj = NULL;
    struct json_object * params = NULL;
    struct json_object * version = NULL;

    if (!json_object_is_type(msg, json_type_object))
    {
        return;
    }

    if (!json_object_object_get_ex(msg, "jsonrpc", &version) || strcmp(json_object_get_string(version), "2.0") != 0)
    {
        /* Not JSON-RPC 2.0 */
        return;
    }

    json_object_object_get_ex(msg, "id", &id);

    if (!json_object_object_get_ex(msg, "method", &method_obj) || !json_object_is_type(method_obj, json_type_string))
    {
        if (id)
        {
            queue_error_response(svr, id, -32600, "Invalid Request");
        }
        return;
    }

    char const * method_name = json_object_get_string(method_obj);
    json_object_object_get_ex(msg, "params", &params);

    rpc_handler_fn handler = NULL;
    for (size_t i = 0; i < svr->registry.count; i++)
    {
        if (strcmp(svr->registry.methods[i].name, method_name) == 0)
        {
            handler = svr->registry.methods[i].handler;
            break;
        }
    }

    if (handler)
    {
        fprintf(
            stderr, "DEBUG: Handling method '%s' (id: %s)\n", method_name, id ? json_object_get_string(id) : "null"
        );
        if (!handler(svr, params, id))
        {
            queue_error_response(svr, id, -32600, "Invalid Request");
        }
    }
    else if (id)
    {
        fprintf(stderr, "DEBUG: Method '%s' not found (id: %s)\n", method_name, json_object_get_string(id));
        queue_error_response(svr, id, -32601, "Method not found");
    }
}

static void
on_parse_complete(void * user_data)
{
    rpc_server_st * const svr = user_data;
    char const cmd = 'C'; // 'C' for Complete
    if (write(svr->completion.pipe[1], &cmd, 1) != 1)
    {
        perror("on_parse_complete: write failed");
    }
}

static void
parse_completion_cb(struct uloop_fd * u, unsigned int events)
{
    UNUSED_PARAM(events);
    rpc_server_st * const svr = container_of(u, rpc_server_st, completion.fd);

    if (u->eof || u->error)
    {
        uloop_end();
        return;
    }

    char cmd;
    bool retry = true;
    do
    {
        if (read(svr->completion.pipe[0], &cmd, 1) < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            else
            {
                perror("parse_completion_cb: read failed");
                uloop_end();
                return;
            }
        }
        retry = false;
    } while (retry);

    epc_parse_session_sync_result(&svr->session);

    if (svr->session.result.is_error)
    {
        fprintf(stderr, "Parse Error: %s\n", svr->session.result.data.error->message);
    }
    else
    {
        epc_ast_hook_registry_t * registry = epc_ast_hook_registry_create(JSON_AST_ACTION_COUNT__);
        json_ast_hook_registry_init(registry);
        void * ast_build_user_data = NULL;
        epc_ast_result_t ast_result = epc_ast_build(svr->session.result.data.success, registry, ast_build_user_data);

        if (!ast_result.has_error)
        {
            json_node_t * node = ast_result.ast_root;
            struct json_object * json = node->obj;

            handle_rpc_message(svr, json);

            json_node_free(ast_result.ast_root, ast_build_user_data);
        }
        epc_ast_hook_registry_free(registry);
    }
    if (!epc_parse_session_advance(&svr->session, svr->parser))
    {
        svr->eof_reached = true;
        check_exit_condition(svr);
    }
}

static void
stdin_cb(struct uloop_fd * u, unsigned int events)
{
    UNUSED_PARAM(events);
    rpc_server_st * const svr = container_of(u, rpc_server_st, stdin_fd);

    if (u->eof)
    {
        epc_streaming_notify_eof(&svr->session);
        uloop_fd_delete(u);
    }
    else if (u->error)
    {
        epc_streaming_notify_error(&svr->session, EOF);
        uloop_fd_delete(u);
    }
    else
    {
        epc_streaming_notify_readable(&svr->session);
    }
}

static void
run(rpc_server_st * const svr)
{
    UNUSED_PARAM(svr);

    uloop_run();
}

void
run_server(rpc_server_st * const svr, int const in_fd, int const out_fd)
{
    svr->out_fd = out_fd;
    svr->in_fd = in_fd;

    /* Set output to non-blocking. */
    int flags = fcntl(out_fd, F_GETFL, 0);
    fcntl(out_fd, F_SETFL, flags | O_NONBLOCK);

    epc_parser_list * list = epc_parser_list_create();

    svr->parser = create_json_parser(list);
    if (!svr->parser)
    {
        fprintf(stderr, "Failed to create JSON parser.\n");
        epc_parser_list_free(list);
        exit(EXIT_FAILURE);
    }

    uloop_init();
    INIT_LIST_HEAD(&svr->write_queue);
    runqueue_init(&svr->tool_queue);

    svr->stdin_fd.fd = svr->in_fd;
    svr->stdin_fd.cb = stdin_cb;
    uloop_fd_add(&svr->stdin_fd, ULOOP_READ);

    svr->out_uloop_fd.fd = svr->out_fd;
    svr->out_uloop_fd.cb = write_queue_cb;

    if (pipe(svr->completion.pipe) < 0)
    {
        perror("Failed to create signal pipe");
        exit(EXIT_FAILURE);
    }

    svr->completion.fd.fd = svr->completion.pipe[0];
    svr->completion.fd.cb = parse_completion_cb;
    uloop_fd_add(&svr->completion.fd, ULOOP_READ);

    svr->session = epc_parse_fd_reactive(svr->parser, in_fd, on_parse_complete, svr, NULL);

    run(svr);
    runqueue_kill(&svr->tool_queue);
    uloop_done();

    epc_parse_session_destroy(&svr->session);
    epc_parser_list_free(list);
    close(svr->completion.pipe[0]);
    close(svr->completion.pipe[1]);
    if (in_fd != STDIN_FILENO)
    {
        close(in_fd);
    }
}

#include "server.h"

#include "easy_pc/easy_pc_ast.h"

#include "json.h"
#include "json_ast.h"
#include "json_ast_actions.h"
#include "utils.h"

#include <libubox/uloop.h>
#include <libubus.h>
#include <stddef.h>
#include <stdio.h>

static void
print_json_ast(struct json_object * json)
{
    if (json == NULL)
    {
        return;
    }
    printf("%s\n", json_object_to_json_string(json));
}

/*
 * This callback is called from the parser worker thread.
 * We must be careful about thread safety.
 * Here we just write a byte to a pipe to wake up the main loop.
 */
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
        /* TODO: Try and reopen the file? */
        uloop_end();
        return;
    }

    /* Read the character written to the pipe by the parse_completion callback. */
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

    /*
     * The callback only told us 'something finished'.
     * We MUST call sync_result to move the result from internal storage to session->result.
     */
    epc_parse_session_sync_result(&svr->session);

    if (svr->session.result.is_error)
    {
        fprintf(stderr, "Parse Error: %s\n", svr->session.result.data.error->message);
        fprintf(stderr, "expected: %s\n", svr->session.result.data.error->expected);
        fprintf(stderr, "actual: %s\n", svr->session.result.data.error->found);
    }
    else
    {
        printf("\n---JSON Parsed ---\n");
        char * cpt_str = epc_cpt_to_string(svr->session.internal_parse_ctx, svr->session.result.data.success);
        if (cpt_str)
        {
            printf("%s\n", cpt_str);
            free(cpt_str);
        }
        epc_ast_hook_registry_t * registry = epc_ast_hook_registry_create(JSON_AST_ACTION_COUNT__);
        json_ast_hook_registry_init(registry);
        void * ast_build_user_data = NULL;
        epc_ast_result_t ast_result = epc_ast_build(svr->session.result.data.success, registry, ast_build_user_data);

        if (ast_result.has_error)
        {
            fprintf(stderr, "AST Error: %s\n", ast_result.error_message);
        }
        else
        {
            fprintf(stderr, "AST parsed\n");
            json_node_t * node = ast_result.ast_root;
            struct json_object * json = node->obj;
            print_json_ast(json);
            json_node_free(ast_result.ast_root, ast_build_user_data);
        }
    }
    printf("Advancing to next JSON message...\n");
    if (!epc_parse_session_advance(&svr->session, svr->parser))
    {
        uloop_end();
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
        uloop_end();
    }
    else if (u->error)
    {
        epc_streaming_notify_error(&svr->session, EOF);
        uloop_end();
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
    svr->out_fp = fd_to_out_fp(out_fd);
    svr->in_fd = in_fd;

    epc_parser_list * list = epc_parser_list_create();

    svr->parser = create_json_parser(list);
    if (!svr->parser)
    {
        fprintf(stderr, "Failed to create JSON parser.\n");
        epc_parser_list_free(list);
        exit(EXIT_FAILURE);
    }

    uloop_init();

    svr->stdin_fd.fd = svr->in_fd;
    svr->stdin_fd.cb = stdin_cb;
    uloop_fd_add(&svr->stdin_fd, ULOOP_READ);

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

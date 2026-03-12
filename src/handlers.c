#include "handlers.h"

#include "server.h"
#include "utils.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <libubox/runqueue.h>
#include <libubox/uloop.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void
queue_success_response(rpc_server_st * svr, struct json_object * id, struct json_object * result)
{
    struct json_object * res = json_object_new_object();
    json_object_object_add(res, "jsonrpc", json_object_new_string("2.0"));
    json_object_object_add(res, "result", result);
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

static bool
handle_initialize(rpc_server_st * svr, struct json_object * params, struct json_object * id)
{
    (void)params;
    struct json_object * result = json_object_new_object();
    json_object_object_add(result, "protocolVersion", json_object_new_string("2024-11-05"));

    struct json_object * capabilities = json_object_new_object();
    struct json_object * tools = json_object_new_object();
    json_object_object_add(tools, "listChanged", json_object_new_boolean(false));
    json_object_object_add(capabilities, "tools", tools);
    json_object_object_add(result, "capabilities", capabilities);

    struct json_object * server_info = json_object_new_object();
    json_object_object_add(server_info, "name", json_object_new_string("My First MCP Server in C"));
    json_object_object_add(server_info, "version", json_object_new_string("0.1.0"));
    json_object_object_add(result, "serverInfo", server_info);

    queue_success_response(svr, id, result);

    return true;
}

typedef struct tool_definition_st tool_definition_st;
struct tool_definition_st
{
    char const * name;
    char const * description;
    struct json_object * (*list_handler_cb)(tool_definition_st const * definition, rpc_server_st * svr);
    void (*run_handler_cb)(
        tool_definition_st const * definition, rpc_server_st * svr, struct json_object * params, int out_fd
    );
};

static struct json_object *
echo_list_cb(tool_definition_st const * definition, rpc_server_st * svr)
{
    (void)svr;

    struct json_object * tool = json_object_new_object();
    json_object_object_add(tool, "name", json_object_new_string(definition->name));
    json_object_object_add(tool, "description", json_object_new_string(definition->description));

    struct json_object * input_schema = json_object_new_object();
    json_object_object_add(input_schema, "type", json_object_new_string("object"));

    struct json_object * properties = json_object_new_object();
    struct json_object * message_prop = json_object_new_object();
    json_object_object_add(message_prop, "type", json_object_new_string("string"));
    json_object_object_add(properties, "message", message_prop);

    struct json_object * delay_prop = json_object_new_object();
    json_object_object_add(delay_prop, "type", json_object_new_string("integer"));
    json_object_object_add(
        delay_prop, "description", json_object_new_string("Optional delay in seconds before responding")
    );
    json_object_object_add(properties, "delay", delay_prop);

    json_object_object_add(input_schema, "properties", properties);
    json_object_object_add(tool, "inputSchema", input_schema);

    return tool;
}

static void
echo_run_cb(tool_definition_st const * definition, rpc_server_st * svr, struct json_object * params, int out_fd)
{
    (void)definition;
    (void)svr;

    struct json_object * args = NULL;
    json_object_object_get_ex(params, "arguments", &args);

    struct json_object * delay_obj = NULL;
    json_object_object_get_ex(args, "delay", &delay_obj);

    if (delay_obj && json_object_is_type(delay_obj, json_type_int))
    {
        sleep(json_object_get_int(delay_obj));
    }

    struct json_object * message_obj = NULL;
    json_object_object_get_ex(args, "message", &message_obj);
    char const * msg = message_obj ? json_object_get_string(message_obj) : "";
    dprintf(out_fd, "%s", msg);
}

static struct json_object *
my_ip_list_cb(tool_definition_st const * definition, rpc_server_st * svr)
{
    (void)svr;

    struct json_object * tool = json_object_new_object();
    json_object_object_add(tool, "name", json_object_new_string(definition->name));
    json_object_object_add(tool, "description", json_object_new_string(definition->description));

    struct json_object * input_schema = json_object_new_object();
    json_object_object_add(input_schema, "type", json_object_new_string("object"));

    struct json_object * properties = json_object_new_object();
    json_object_object_add(input_schema, "properties", properties);
    json_object_object_add(tool, "inputSchema", input_schema);

    return tool;
}

static void
my_ip_run_cb(tool_definition_st const * definition, rpc_server_st * svr, struct json_object * params, int out_fd)
{
    (void)definition;
    (void)svr;
    (void)params;

    struct ifaddrs * addrs;
    char const * ip = "unknown";
    char buf[INET_ADDRSTRLEN];

    if (getifaddrs(&addrs) == 0)
    {
        for (struct ifaddrs * tmp = addrs; tmp != NULL; tmp = tmp->ifa_next)
        {
            if (tmp->ifa_addr && tmp->ifa_addr->sa_family == AF_INET)
            {
                struct sockaddr_in * pAddr = (struct sockaddr_in *)tmp->ifa_addr;
                inet_ntop(AF_INET, &pAddr->sin_addr, buf, sizeof(buf));
                if (strcmp(buf, "127.0.0.1") != 0)
                {
                    ip = buf;
                    break;
                }
            }
        }
    }
    if (addrs != NULL)
    {
        freeifaddrs(addrs);
    }

    dprintf(out_fd, "%s", ip);
}

static struct json_object *
get_date_list_cb(tool_definition_st const * definition, rpc_server_st * svr)
{
    (void)svr;

    struct json_object * tool = json_object_new_object();
    json_object_object_add(tool, "name", json_object_new_string(definition->name));
    json_object_object_add(tool, "description", json_object_new_string(definition->description));

    struct json_object * input_schema = json_object_new_object();
    json_object_object_add(input_schema, "type", json_object_new_string("object"));

    struct json_object * properties = json_object_new_object();
    struct json_object * utc_prop = json_object_new_object();
    json_object_object_add(utc_prop, "type", json_object_new_string("boolean"));
    json_object_object_add(utc_prop, "description", json_object_new_string("Display time in UTC"));
    json_object_object_add(properties, "utc", utc_prop);

    json_object_object_add(input_schema, "properties", properties);
    json_object_object_add(tool, "inputSchema", input_schema);

    return tool;
}

static void
get_date_run_cb(tool_definition_st const * definition, rpc_server_st * svr, struct json_object * params, int out_fd)
{
    (void)definition;
    (void)svr;

    struct json_object * args = NULL;
    json_object_object_get_ex(params, "arguments", &args);

    struct json_object * utc_obj = NULL;
    bool is_utc = false;
    if (json_object_object_get_ex(args, "utc", &utc_obj))
    {
        is_utc = json_object_get_boolean(utc_obj);
    }

    if (dup2(out_fd, STDOUT_FILENO) < 0)
    {
        perror("dup2 failed");
        return;
    }
    close(out_fd);

    if (is_utc)
    {
        execlp("date", "date", "-u", (char *)NULL);
    }
    else
    {
        execlp("date", "date", (char *)NULL);
    }

    perror("execlp failed");
}

static struct json_object *
ps_list_cb(tool_definition_st const * definition, rpc_server_st * svr)
{
    (void)svr;

    struct json_object * tool = json_object_new_object();
    json_object_object_add(tool, "name", json_object_new_string(definition->name));
    json_object_object_add(tool, "description", json_object_new_string(definition->description));

    struct json_object * input_schema = json_object_new_object();
    json_object_object_add(input_schema, "type", json_object_new_string("object"));

    struct json_object * properties = json_object_new_object();

    struct json_object * all_prop = json_object_new_object();
    json_object_object_add(all_prop, "type", json_object_new_string("boolean"));
    json_object_object_add(all_prop, "description", json_object_new_string("Show processes for all users ('a')"));
    json_object_object_add(properties, "all", all_prop);

    struct json_object * user_prop = json_object_new_object();
    json_object_object_add(user_prop, "type", json_object_new_string("boolean"));
    json_object_object_add(user_prop, "description", json_object_new_string("Display user-oriented format ('u')"));
    json_object_object_add(properties, "user", user_prop);

    struct json_object * extra_prop = json_object_new_object();
    json_object_object_add(extra_prop, "type", json_object_new_string("boolean"));
    json_object_object_add(
        extra_prop, "description", json_object_new_string("Show processes not attached to a tty ('x')")
    );
    json_object_object_add(properties, "extra", extra_prop);

    json_object_object_add(input_schema, "properties", properties);
    json_object_object_add(tool, "inputSchema", input_schema);

    return tool;
}

static void
ps_run_cb(tool_definition_st const * definition, rpc_server_st * svr, struct json_object * params, int out_fd)
{
    (void)definition;
    (void)svr;

    struct json_object * args = NULL;
    json_object_object_get_ex(params, "arguments", &args);

    bool a = false, u = false, x = false;
    struct json_object * tmp;
    if (json_object_object_get_ex(args, "all", &tmp))
    {
        a = json_object_get_boolean(tmp);
    }
    if (json_object_object_get_ex(args, "user", &tmp))
    {
        u = json_object_get_boolean(tmp);
    }
    if (json_object_object_get_ex(args, "extra", &tmp))
    {
        x = json_object_get_boolean(tmp);
    }

    char flags[4] = { 0 };
    int i = 0;
    if (a)
    {
        flags[i++] = 'a';
    }
    if (u)
    {
        flags[i++] = 'u';
    }
    if (x)
    {
        flags[i++] = 'x';
    }

    if (dup2(out_fd, STDOUT_FILENO) < 0)
    {
        perror("dup2 failed");
        return;
    }
    close(out_fd);

    char * argv[3];
    argv[0] = "ps";
    if (i > 0)
    {
        argv[1] = flags;
        argv[2] = NULL;
    }
    else
    {
        argv[1] = NULL;
    }

    execvp("ps", argv);
    perror("execvp failed");
}

static tool_definition_st const tool_definitions[] = {
    {
        .name = "echo",
        .description = "Echoes back the input",
        .list_handler_cb = echo_list_cb,
        .run_handler_cb = echo_run_cb,
    },
    {
        .name = "My_IP_address",
        .description = "Returns my local IP address",
        .list_handler_cb = my_ip_list_cb,
        .run_handler_cb = my_ip_run_cb,
    },
    {
        .name = "get_date",
        .description = "Returns the current date and time",
        .list_handler_cb = get_date_list_cb,
        .run_handler_cb = get_date_run_cb,
    },
    {
        .name = "ps",
        .description = "List running processes",
        .list_handler_cb = ps_list_cb,
        .run_handler_cb = ps_run_cb,
    },
};

static tool_definition_st const *
tool_definition_lookup(char const * name)
{
    if (name == NULL)
    {
        return NULL;
    }

    for (size_t i = 0; i < sizeof(tool_definitions) / sizeof(tool_definitions[0]); i++)
    {
        tool_definition_st const * def = &tool_definitions[i];
        if (strcmp(def->name, name) == 0)
        {
            return def;
        }
    }
    return NULL;
}

static bool
handle_list_tools(rpc_server_st * svr, struct json_object * params, struct json_object * id)
{
    (void)params;
    struct json_object * result = json_object_new_object();
    struct json_object * tools_array = json_object_new_array();

    for (size_t i = 0; i < sizeof(tool_definitions) / sizeof(tool_definitions[0]); i++)
    {
        tool_definition_st const * def = &tool_definitions[i];

        json_object_array_add(tools_array, def->list_handler_cb(def, svr));
    }

    json_object_object_add(result, "tools", tools_array);

    queue_success_response(svr, id, result);

    return true;
}

typedef struct tool_call_context_st
{
    rpc_server_st * svr;
    struct runqueue_process run_proc;
    struct uloop_fd pipe_fd;
    struct json_object * id;
    struct json_object * params;
    tool_definition_st const * def;
    char * output;
    size_t output_len;
    bool was_cancelled;
} tool_call_context_st;

static void
tool_call_pipe_cb(struct uloop_fd * u, unsigned int events)
{
    UNUSED_PARAM(events);
    tool_call_context_st * ctx = container_of(u, tool_call_context_st, pipe_fd);
    char buf[1024];
    ssize_t n;

    while ((n = read(u->fd, buf, sizeof(buf))) > 0)
    {
        char * new_output = realloc(ctx->output, ctx->output_len + n + 1);
        if (!new_output)
        {
            perror("realloc failed");
            return;
        }
        ctx->output = new_output;
        memcpy(ctx->output + ctx->output_len, buf, n);
        ctx->output_len += n;
        ctx->output[ctx->output_len] = '\0';
    }

    if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK))
    {
        uloop_fd_delete(u);
        close(u->fd);
        u->fd = -1;
    }
}

static void
queue_success_content(rpc_server_st * svr, struct json_object * id, char const * output)
{
    struct json_object * content_array;

    if (output != NULL)
    {
        struct json_object * content = json_object_new_object();
        json_object_object_add(content, "type", json_object_new_string("text"));
        json_object_object_add(content, "text", json_object_new_string(output));

        content_array = json_object_new_array();
        json_object_array_add(content_array, content);
    }
    else
    {
        content_array = json_object_new_array();
    }

    struct json_object * result = json_object_new_object();

    json_object_object_add(result, "content", content_array);
    queue_success_response(svr, id, result);
}

static void
tool_call_task_complete_cb(struct runqueue * q, struct runqueue_task * t)
{
    (void)q;
    tool_call_context_st * ctx = container_of(t, tool_call_context_st, run_proc.task);

    fprintf(
        stderr,
        "DEBUG: Tool task completed for (id: %s)%s\n",
        ctx->id ? json_object_get_string(ctx->id) : "null",
        ctx->was_cancelled ? " [CANCELLED]" : ""
    );

    /* Ensure pipe is cleaned up. */
    if (ctx->pipe_fd.fd != -1)
    {
        /* Final read if not cancelled. */
        if (!ctx->was_cancelled)
        {
            tool_call_pipe_cb(&ctx->pipe_fd, ULOOP_READ);
        }

        /* If still open after final read (or if cancelled), close it now. */
        if (ctx->pipe_fd.fd != -1)
        {
            uloop_fd_delete(&ctx->pipe_fd);
            close(ctx->pipe_fd.fd);
            ctx->pipe_fd.fd = -1;
        }
    }

    if (ctx->was_cancelled)
    {
        goto cleanup;
    }

    queue_success_content(ctx->svr, ctx->id, ctx->output);

cleanup:
    json_object_put(ctx->id);
    json_object_put(ctx->params);
    free(ctx->output);
    free(ctx);
}

static void
tool_call_run_cb(struct runqueue * q, struct runqueue_task * t)
{
    tool_call_context_st * ctx = container_of(t, tool_call_context_st, run_proc.task);
    int pipefds[2];

    if (pipe(pipefds) < 0)
    {
        perror("pipe failed");
        return;
    }

    pid_t pid = fork();
    if (pid < 0)
    {
        perror("fork failed");
        close(pipefds[0]);
        close(pipefds[1]);
        return;
    }

    if (pid == 0)
    {
        /* Child */
        close(pipefds[0]);
        ctx->def->run_handler_cb(ctx->def, ctx->svr, ctx->params, pipefds[1]);
        close(pipefds[1]);
        exit(0);
    }

    /* Parent */
    fprintf(
        stderr,
        "DEBUG: Forked process %d for tool '%s' (id: %s)\n",
        (int)pid,
        ctx->def->name,
        ctx->id ? json_object_get_string(ctx->id) : "null"
    );
    close(pipefds[1]);
    int flags = fcntl(pipefds[0], F_GETFL, 0);
    fcntl(pipefds[0], F_SETFL, flags | O_NONBLOCK);

    ctx->pipe_fd.fd = pipefds[0];
    uloop_fd_add(&ctx->pipe_fd, ULOOP_READ);

    runqueue_process_add(q, &ctx->run_proc, pid);
}

static void
tool_call_cancel_cb(struct runqueue * q, struct runqueue_task * t, int type)
{
    tool_call_context_st * ctx = container_of(t, tool_call_context_st, run_proc.task);
    ctx->was_cancelled = true;
    runqueue_process_cancel_cb(q, t, type);
}

static const struct runqueue_task_type tool_call_type = {
    .run = tool_call_run_cb,
    .cancel = tool_call_cancel_cb,
    .kill = runqueue_process_kill_cb,
};

struct lookup_ctx
{
    struct json_object * id;
    tool_call_context_st * call_ctx;
};

static int
lookup_call_ctx_cb(void * ptr, struct safe_list * list)
{
    struct lookup_ctx * ctx = ptr;
    struct json_object * id = ctx->id;
    struct runqueue_task * t = container_of(list, struct runqueue_task, list);
    tool_call_context_st * call_ctx = container_of(t, tool_call_context_st, run_proc.task);
    struct json_object * call_id = call_ctx->id;

    if (json_object_get_type(call_id) == json_object_get_type(id)
        || strcmp(json_object_get_string(call_id), json_object_get_string(id)) == 0)
    {
        ctx->call_ctx = call_ctx;
    }

    return ctx->call_ctx != NULL; /* Stop iteration if the context has been found. */
}

tool_call_context_st *
call_ctx_lookup_by_id(struct json_object * id, struct safe_list * list)
{
    struct lookup_ctx ctx = { .id = id };

    safe_list_for_each(list, lookup_call_ctx_cb, &ctx);
    return ctx.call_ctx;
}

static bool
handle_cancel_request(rpc_server_st * svr, struct json_object * params, struct json_object * id)
{
    (void)id;
    struct json_object * request_id_obj = NULL;
    if (!json_object_object_get_ex(params, "requestId", &request_id_obj))
    {
        return false;
    }
    tool_call_context_st * call_ctx = call_ctx_lookup_by_id(request_id_obj, &svr->tool_queue.tasks_active);
    if (call_ctx == NULL)
    {
        call_ctx = call_ctx_lookup_by_id(request_id_obj, &svr->tool_queue.tasks_inactive);
    }

    if (call_ctx != NULL)
    {
        fprintf(
            stderr,
            "DEBUG: Cancelling request %s (pid: %d)\n",
            json_object_get_string(request_id_obj),
            (int)call_ctx->run_proc.proc.pid
        );
        call_ctx->was_cancelled = true;

        struct runqueue_task * t = &call_ctx->run_proc.task;

        runqueue_task_cancel(t, SIGTERM);
    }
    else
    {
        fprintf(
            stderr,
            "DEBUG: Request %s (%s) not found\n",
            json_object_get_string(request_id_obj),
            json_type_to_name(json_object_get_type(request_id_obj))
        );
    }
    return true;
}

static bool
handle_call_tool(rpc_server_st * svr, struct json_object * params, struct json_object * id)
{
    struct json_object * name_obj = NULL;
    if (!json_object_object_get_ex(params, "name", &name_obj))
    {
        return false;
    }

    char const * name = json_object_get_string(name_obj);
    fprintf(stderr, "DEBUG: Queuing call for tool '%s' (id: %s)\n", name, id ? json_object_get_string(id) : "null");

    tool_definition_st const * def = tool_definition_lookup(name);
    if (def == NULL)
    {
        fprintf(stderr, "DEBUG: Tool '%s' not found\n", name);
        return false;
    }

    tool_call_context_st * ctx = calloc(1, sizeof(tool_call_context_st));
    ctx->svr = svr;
    ctx->def = def;
    ctx->id = id != NULL ? json_object_get(id) : NULL;
    ctx->params = params != NULL ? json_object_get(params) : NULL;
    ctx->pipe_fd.cb = tool_call_pipe_cb;
    ctx->pipe_fd.fd = -1;
    ctx->run_proc.task.type = &tool_call_type;
    ctx->run_proc.task.complete = tool_call_task_complete_cb;

    runqueue_task_add(&svr->tool_queue, &ctx->run_proc.task, false);

    return true;
}

void
rpc_server_register_handlers(rpc_server_st * svr)
{
    rpc_server_register_method(svr, "initialize", handle_initialize);
    rpc_server_register_method(svr, "tools/list", handle_list_tools);
    rpc_server_register_method(svr, "tools/call", handle_call_tool);
    rpc_server_register_method(svr, "notifications/cancelled", handle_cancel_request);
}

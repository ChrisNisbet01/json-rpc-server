#include "server.h"
#include "utils.h"

#include <errno.h>
#include <fcntl.h>
#include <libubox/uloop.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <netinet/in.h>

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

    struct json_object * content_array = json_object_new_array();

    struct json_object * args = NULL;
    json_object_object_get_ex(params, "arguments", &args);
    struct json_object * message_obj = NULL;
    json_object_object_get_ex(args, "message", &message_obj);
    struct json_object * delay_obj = NULL;
    json_object_object_get_ex(args, "delay", &delay_obj);

    if (delay_obj && json_object_is_type(delay_obj, json_type_int))
    {
        sleep(json_object_get_int(delay_obj));
    }

    struct json_object * content = json_object_new_object();
    json_object_object_add(content, "type", json_object_new_string("text"));
    char const * msg = message_obj ? json_object_get_string(message_obj) : "";
    json_object_object_add(content, "text", json_object_new_string(msg));
    json_object_array_add(content_array, content);

    dprintf(out_fd, "%s", json_object_to_json_string(content_array));
    json_object_put(content_array);
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

    struct json_object * content = json_object_new_object();
    json_object_object_add(content, "type", json_object_new_string("text"));
    json_object_object_add(content, "text", json_object_new_string(ip));

    if (addrs)
    {
        freeifaddrs(addrs);
    }

    struct json_object * content_array = json_object_new_array();
    json_object_array_add(content_array, content);

    dprintf(out_fd, "%s", json_object_to_json_string(content_array));
    json_object_put(content_array);
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
    struct list_head list;
    rpc_server_st * svr;
    struct uloop_process proc;
    struct uloop_fd pipe_fd;
    struct json_object * id;
    char * output;
    size_t output_len;
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
tool_call_proc_cb(struct uloop_process * c, int ret)
{
    UNUSED_PARAM(ret);
    tool_call_context_st * ctx = container_of(c, tool_call_context_st, proc);

    fprintf(
        stderr,
        "DEBUG: Tool process (pid: %d) exited for (id: %s)\n",
        (int)ctx->proc.pid,
        ctx->id ? json_object_get_string(ctx->id) : "null"
    );

    /* If there's still data in the pipe, read it. */
    if (ctx->pipe_fd.fd != -1)
    {
        tool_call_pipe_cb(&ctx->pipe_fd, ULOOP_READ);
    }

    struct json_object * result = json_object_new_object();
    struct json_object * content_array = NULL;

    if (ctx->output)
    {
        content_array = json_tokener_parse(ctx->output);
    }

    if (!content_array)
    {
        content_array = json_object_new_array();
    }

    json_object_object_add(result, "content", content_array);
    queue_success_response(ctx->svr, ctx->id, result);

    if (ctx->id)
    {
        json_object_put(ctx->id);
    }
    list_del(&ctx->list);
    free(ctx->output);
    ctx->svr->pending_tools--;
    free(ctx);
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
    fprintf(stderr, "DEBUG: Handling call for tool '%s' (id: %s)\n", name, id ? json_object_get_string(id) : "null");

    tool_definition_st const * def = tool_definition_lookup(name);
    if (def == NULL)
    {
        fprintf(stderr, "DEBUG: Tool '%s' not found\n", name);
        return false;
    }
    fprintf(stderr, "DEBUG: Tool '%s' found\n", def->name);
    int pipefds[2];
    if (pipe(pipefds) < 0)
    {
        perror("pipe failed");
        return false;
    }

    pid_t pid = fork();
    if (pid < 0)
    {
        perror("fork failed");
        close(pipefds[0]);
        close(pipefds[1]);
        return false;
    }

    if (pid == 0)
    {
        /* Child */
        close(pipefds[0]);
        def->run_handler_cb(def, svr, params, pipefds[1]);
        close(pipefds[1]);
        exit(0);
    }

    /* Parent */
    fprintf(
        stderr,
        "DEBUG: Forked process %d for tool '%s' (id: %s)\n",
        (int)pid,
        name,
        id ? json_object_get_string(id) : "null"
    );
    close(pipefds[1]);
    int flags = fcntl(pipefds[0], F_GETFL, 0);
    fcntl(pipefds[0], F_SETFL, flags | O_NONBLOCK);

    tool_call_context_st * ctx = calloc(1, sizeof(tool_call_context_st));
    ctx->svr = svr;
    ctx->id = id ? json_object_get(id) : NULL;
    ctx->pipe_fd.fd = pipefds[0];
    ctx->pipe_fd.cb = tool_call_pipe_cb;
    ctx->proc.pid = pid;
    ctx->proc.cb = tool_call_proc_cb;

    list_add_tail(&ctx->list, &svr->tool_calls);
    svr->pending_tools++;
    uloop_fd_add(&ctx->pipe_fd, ULOOP_READ);
    uloop_process_add(&ctx->proc);

    return true;
}

static void
usage(FILE * const out_fp, char const * const exe_name)
{
    fprintf(out_fp, "Usage: %s\n", exe_name);
}

static bool
parse_args(rpc_server_st * const svr, int const argc, char ** const argv)
{
    (void)svr;
    int opt;

    while ((opt = getopt(argc, argv, "")) != -1)
    {
        switch (opt)
        {
        case '?': // For unknown options or missing arguments
            usage(stderr, argv[0]);
            return false;
        }
    }

    return true;
}

int
main(int argc, char ** argv)
{
    int exit_code;
    rpc_server_st svr = { 0 };

    if (!parse_args(&svr, argc, argv))
    {
        exit_code = EXIT_FAILURE;
        goto done;
    }

    rpc_server_register_method(&svr, "initialize", handle_initialize);
    rpc_server_register_method(&svr, "tools/list", handle_list_tools);
    rpc_server_register_method(&svr, "tools/call", handle_call_tool);

    run_server(&svr, STDIN_FILENO, STDOUT_FILENO);

    exit_code = svr.exit_code;

done:
    return exit_code;
}

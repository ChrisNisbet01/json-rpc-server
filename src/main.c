#include "server.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static struct json_object *
handle_initialize(rpc_server_st * svr, struct json_object * params, struct json_object * id)
{
    (void)svr;
    (void)params;
    (void)id;
    struct json_object * result = json_object_new_object();
    json_object_object_add(result, "protocolVersion", json_object_new_string("2024-11-05"));

    struct json_object * capabilities = json_object_new_object();
    struct json_object * tools = json_object_new_object();
    json_object_object_add(tools, "listChanged", json_object_new_boolean(false));
    json_object_object_add(capabilities, "tools", tools);
    json_object_object_add(result, "capabilities", capabilities);

    struct json_object * server_info = json_object_new_object();
    json_object_object_add(server_info, "name", json_object_new_string("Example MCP Server"));
    json_object_object_add(server_info, "version", json_object_new_string("0.1.0"));
    json_object_object_add(result, "serverInfo", server_info);

    return result;
}

static struct json_object *
handle_list_tools(rpc_server_st * svr, struct json_object * params, struct json_object * id)
{
    (void)svr;
    (void)params;
    (void)id;
    struct json_object * result = json_object_new_object();
    struct json_object * tools_array = json_object_new_array();

    struct json_object * tool = json_object_new_object();
    json_object_object_add(tool, "name", json_object_new_string("echo"));
    json_object_object_add(tool, "description", json_object_new_string("Echoes back the input"));

    struct json_object * input_schema = json_object_new_object();
    json_object_object_add(input_schema, "type", json_object_new_string("object"));
    struct json_object * properties = json_object_new_object();
    struct json_object * message_prop = json_object_new_object();
    json_object_object_add(message_prop, "type", json_object_new_string("string"));
    json_object_object_add(properties, "message", message_prop);
    json_object_object_add(input_schema, "properties", properties);
    json_object_object_add(tool, "inputSchema", input_schema);

    json_object_array_add(tools_array, tool);
    json_object_object_add(result, "tools", tools_array);

    return result;
}

static struct json_object *
handle_call_tool(rpc_server_st * svr, struct json_object * params, struct json_object * id)
{
    (void)svr;
    (void)id;
    struct json_object * name_obj = NULL;
    if (!json_object_object_get_ex(params, "name", &name_obj))
    {
        return NULL; // Should return an error object really
    }

    char const * name = json_object_get_string(name_obj);
    struct json_object * result = json_object_new_object();
    struct json_object * content_array = json_object_new_array();

    if (strcmp(name, "echo") == 0)
    {
        struct json_object * args = NULL;
        json_object_object_get_ex(params, "arguments", &args);
        struct json_object * message_obj = NULL;
        json_object_object_get_ex(args, "message", &message_obj);

        struct json_object * content = json_object_new_object();
        json_object_object_add(content, "type", json_object_new_string("text"));
        char const * msg = message_obj ? json_object_get_string(message_obj) : "";
        json_object_object_add(content, "text", json_object_new_string(msg));
        json_object_array_add(content_array, content);
    }

    json_object_object_add(result, "content", content_array);
    return result;
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
    rpc_server_st svr = {0};

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

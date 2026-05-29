#pragma once

#include <json-c/json.h>
#include <stdbool.h>

typedef struct rpc_server_st rpc_server_st;

typedef bool (*rpc_handler_fn)(rpc_server_st * svr, struct json_object * params, struct json_object * id);

void rpc_register_method(rpc_server_st * svr, char const * name, rpc_handler_fn handler);

void rpc_dispatch(rpc_server_st * svr, struct json_object * msg);

void rpc_send_response(rpc_server_st * svr, struct json_object * id, struct json_object * result);

void rpc_send_error(rpc_server_st * svr, struct json_object * id, int code, char const * message);

void rpc_on_transport_msg(char const * body, size_t len, void * user_data);

void rpc_cleanup_registry(rpc_server_st * svr);

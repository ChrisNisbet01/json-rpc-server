#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef struct rpc_server_st rpc_server_st;

void transport_init(rpc_server_st * svr);
void transport_cleanup(rpc_server_st * svr);
void transport_send(rpc_server_st * svr, char const * data, size_t len);
void transport_close_stdin(rpc_server_st * svr);
bool transport_can_exit(rpc_server_st * svr);

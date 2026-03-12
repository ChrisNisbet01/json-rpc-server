#pragma once

#include "server.h"

/*
 * Registers all RPC method handlers for the server.
 * This includes basic MCP lifecycle methods and tool-related methods.
 */
void rpc_server_register_handlers(rpc_server_st * svr);

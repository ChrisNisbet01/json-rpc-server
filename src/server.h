#pragma once

#include "framing.h"

#include <json-c/json.h>
#include <libubox/list.h>
#include <libubox/runqueue.h>
#include <libubox/uloop.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

typedef struct rpc_server_st rpc_server_st;

typedef bool (*rpc_handler_fn)(rpc_server_st * svr, struct json_object * params, struct json_object * id);

typedef struct rpc_method_st
{
    char * name;
    rpc_handler_fn handler;
} rpc_method_st;

typedef struct rpc_method_registry_st
{
    rpc_method_st * methods;
    size_t count;
    size_t capacity;
} rpc_method_registry_st;

typedef void (*transport_msg_cb)(char const * body, size_t len, void * user_data);

struct rpc_server_st
{
    /* --- Transport layer: I/O + pluggable framing --- */
    int in_fd;
    int out_fd;

    struct uloop_fd stdin_fd;
    struct uloop_fd out_uloop_fd;
    struct list_head write_queue;

    char * buf;
    size_t buf_len;
    size_t buf_cap;

    framing_st * framing;

    transport_msg_cb on_transport_msg;
    void * on_transport_msg_data;

    bool eof_reached;

    /* --- RPC layer: method registry --- */
    rpc_method_registry_st registry;

    /* --- Application state --- */
    struct runqueue tool_queue;

    bool shutdown_requested;
    int exit_code;
};

void run_server(rpc_server_st * svr, int const in_fd, int const out_fd);

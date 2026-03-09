#pragma once

#include <libubox/uloop.h>
#include <easy_pc/easy_pc.h>

#include <stdio.h>

// TODO: Need libubox includes for struct uloop_fd

typedef struct rpc_server_st
{
    int in_fd;
    int out_fd;
    FILE *out_fp;

    struct uloop_fd stdin_fd;
    struct
    {
        struct uloop_fd fd;
        int pipe[2];
    } completion;

    epc_parser_t *parser;
    epc_parse_session_t session;

    int exit_code;
} rpc_server_st;

void run_server(rpc_server_st *const svr, int const in_fd, int const out_fd);

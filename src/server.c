#include "server.h"

#include "handlers.h"
#include "rpc.h"
#include "transport.h"
#include "utils.h"

#include <libubox/uloop.h>
#include <stdio.h>
#include <stdlib.h>

void
run_server(rpc_server_st * const svr, int const in_fd, int const out_fd)
{
    fprintf(stderr, "[MCP] Server starting on in_fd=%d, out_fd=%d\n", in_fd, out_fd);
    svr->out_fd = out_fd;
    svr->in_fd = in_fd;

    svr->framing = framing_newline_create();

    uloop_init();
    runqueue_init(&svr->tool_queue);
    svr->tool_queue.max_running_tasks = 4;

    svr->on_transport_msg = rpc_on_transport_msg;
    svr->on_transport_msg_data = svr;

    transport_init(svr);

    rpc_server_register_handlers(svr);

    uloop_run();

    rpc_cleanup_registry(svr);
    runqueue_kill(&svr->tool_queue);
    transport_cleanup(svr);

    if (svr->framing)
    {
        svr->framing->destroy(svr->framing);
        svr->framing = NULL;
    }

    uloop_done();

    if (in_fd != STDIN_FILENO)
    {
        close(in_fd);
    }
}

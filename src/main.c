#include "server.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

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

    run_server(&svr, STDIN_FILENO, STDOUT_FILENO);

    exit_code = svr.exit_code;

done:
    return exit_code;
}

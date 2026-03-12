#include "utils.h"

#include <stdio.h>
#include <time.h>
#include <unistd.h>

FILE *
fd_to_out_fp(int const fd)
{
    FILE * out_fp;
    int const new_fd = dup(fd);

    if (new_fd == -1)
    {
        out_fp = NULL;
        perror("dup");
        goto done;
    }

    out_fp = fdopen(new_fd, "w");
    if (out_fp == NULL)
    {
        close(new_fd);
        perror("fdopen");
        goto done;
    }

done:
    return out_fp;
}

void
current_timestamp_str(char * buf, size_t len)
{
    time_t now = time(NULL);
    struct tm * tm_info = localtime(&now);

    strftime(buf, len, "%Y-%m-%dT%H:%M:%S%z", tm_info);
}

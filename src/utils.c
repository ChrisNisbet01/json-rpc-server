#include "utils.h"

#include <unistd.h>

FILE *fd_to_out_fp(int const fd)
{
    FILE *out_fp;
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

#pragma once

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#define UNUSED_PARAM(p) ((void)(p))

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#endif

static inline void
free_const(void const * p)
{
    free((void *)p);
}

static inline bool
str_is_empty(char const * const str)
{
    return str == NULL || str[0] == '\0';
}

FILE * fd_to_out_fp(int const fd);


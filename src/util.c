/*
 * Copyright 2016 Bram Gerritsen
 *
 * This file is part of vtrie.
 *
 * vtrie is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * vtrie is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with vtrie.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <signal.h>
#include "util.h"

/*
 * Do not clean at exit when debugging.
 */
#ifdef NDEBUG
#define EXIT(CODE)\
    do {exit((CODE));} while (0)
#else
#define EXIT(CODE)\
    do {(void)(CODE); abort();} while (0)
#endif

void
die(int exit_code, const char *msg, ...)
{
    va_list va;
    va_start(va, msg);
    vfprintf(stderr, msg, va);
    va_end(va);
    fputc('\n', stderr);
    EXIT(exit_code);
}

void *
safe_malloc(size_t size)
{
    void *result = malloc(size);
    if (result == NULL){
        perror("malloc failed.");
        die(1, "Unable to allocate memory");
    }
    return result;
}

void *
safe_calloc(size_t nitems, size_t size)
{
    void *result = calloc(nitems, size);
    if (result == NULL){
        perror("calloc failed.");
        die(1, "Unable to allocate memory");
    }
    return result;
}

void *
safe_realloc(void *ptr, size_t size)
{
    void *result = realloc(ptr, size);
    if (result == NULL){
        perror("realloc failed.");
        die(1, "Unable to reallocate memory");
    }
    return result;
}

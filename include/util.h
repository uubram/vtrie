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
#ifndef UTIL_H
#define UTIL_H
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>

void die(int exit_code, const char *msg, ...);
void *safe_malloc(size_t size);
void *safe_calloc(size_t nitems, size_t size);
void *safe_realloc(void *ptr, size_t size);

#endif /* defined UTIL_H */

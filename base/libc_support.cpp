/*****************************************************************************
 * Author:   Rian Hunter <rian@alum.mit.edu>
 *
 *****************************************************************************
 * Copyright (c) 2013, Dropbox, Inc.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

// libc_support.cpp: common libc functions that may be missing

#include "base/libc_support.h"

#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {

char *strdup_x(const char *x) {
#ifndef _WIN32
  return strdup(x);
#else
  size_t len = strlen(x);
  char *toret = (char *)malloc(len + 1);
  memcpy(toret, x, len);
  toret[len] = '\0';
  return toret;
#endif
}

int vasprintf_x(char **ret, const char *format, va_list ap) {
#ifndef _WIN32
  return vasprintf(ret, format, ap);
#else
  // initialize pointer to null
  size_t cur_size = 1;
  *ret = NULL;

  int ret_vsnprintf;
  do {
    // reallocate buffer
    cur_size *= 2;
    char *const newp = (char *)realloc(*ret, cur_size);
    if (!newp) {
      free(*ret);
      return -1;
    }
    *ret = newp;

    ret_vsnprintf = vsnprintf(*ret, cur_size, format, ap);
    if (ret_vsnprintf < 0) {
      free(*ret);
      return -1;
    }
  } while ((size_t)ret_vsnprintf >= cur_size);

  return 0;
#endif
}
}

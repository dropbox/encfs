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

#ifndef __c_support_h
#define __c_support_h

#include <cstdarg>

#ifdef __cplusplus
extern "C" {
#endif

char *strdup_x(const char *x);

int vasprintf_x(char **ret, const char *format, va_list ap);

#ifdef __cplusplus
}
#endif

#endif

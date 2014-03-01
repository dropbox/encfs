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

#include "base/logging.h"

#include <iostream>

extern "C" {

static void encfs_default_log_print(const char *filename, int lineno,
                                    encfs_log_level_t level,
                                    const char *toprint) {
  // just default to printing to stderr for now
  (void)filename;
  (void)lineno;
  (void)level;
  std::cerr << toprint << std::endl;
}

encfs_log_level_t _CUR_LEVEL = ENCFS_LOG_DEBUG;
encfs_log_printer_t _LOG_PRINTER = encfs_default_log_print;

void encfs_set_log_printer(encfs_log_printer_t printer) {
  _LOG_PRINTER = printer;
}

void encfs_set_log_level(encfs_log_level_t level) { _CUR_LEVEL = level; }
}

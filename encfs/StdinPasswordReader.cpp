/*****************************************************************************
 * Author:   Valient Gough <vgough@pobox.com>
 * Author:   Rian Hunter <rian@alum.mit.edu>
 *
 *****************************************************************************
 * Copyright (c) 2004-2012, Valient Gough
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

#include <cstdio>
#include <cstring>

#include "encfs/StdinPasswordReader.h"

namespace encfs {

SecureMem *StdinPasswordReader::readPassword(size_t maxLen, bool /*newPass*/) {
  SecureMem *buf = new SecureMem(maxLen);

  char *res = fgets((char *)buf->data(), buf->size(), stdin);
  if (res) {
    // Kill the trailing newline.
    int last = strnlen((char *)buf->data(), buf->size());
    if (last > 0 && buf->data()[last - 1] == '\n') buf->data()[last - 1] = '\0';
  }

  return buf;
}
}

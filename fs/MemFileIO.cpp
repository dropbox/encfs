
/*****************************************************************************
 * Author:   Valient Gough <vgough@pobox.com>
 *
 *****************************************************************************
 * Copyright (c) 2012 Valient Gough
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "base/optional.h"
#include "base/logging.h"
#include "base/Error.h"

#include "fs/FsIO.h"
#include "fs/MemFileIO.h"

#include <limits>

namespace encfs {

static Interface MemFileIO_iface = makeInterface("FileIO/Mem", 1, 0, 0);

MemFileIO::MemFileIO(int size) { buf.resize(size); }

MemFileIO::~MemFileIO() {}

Interface MemFileIO::interface() const { return MemFileIO_iface; }

void MemFileIO::setFileName(const char* name) { this->name = name; }

const char* MemFileIO::getFileName() const { return name.c_str(); }

FsFileAttrs MemFileIO::get_attrs() const {
  assert(buf.size() <=
         std::numeric_limits<decltype(FsFileAttrs().size)>::max());
  return {
      /*.type =*/FsFileType::REGULAR,
      /*.mtime =*/0,
      /*.size =*/(fs_off_t)buf.size(),
      /*.file_id =*/0,
      /*.posix =*/opt::nullopt, };
}

size_t MemFileIO::read(const IORequest& req) const {
  rAssert(req.offset >= 0);

  assert(req.dataLen <= std::numeric_limits<decltype(req.offset)>::max());
  auto amt_to_read = static_cast<decltype(req.offset)>(req.dataLen);

  if (req.offset + amt_to_read > get_attrs().size) {
    amt_to_read = get_attrs().size - req.offset;
    if (amt_to_read < 0) amt_to_read = 0;
  }

  memcpy(req.data, &buf[req.offset], amt_to_read);
  return amt_to_read;
}

void MemFileIO::write(const IORequest& req) {
  rAssert(req.offset >= 0);

  assert(req.dataLen <= std::numeric_limits<decltype(req.offset)>::max());
  auto amt_to_write = static_cast<decltype(req.offset)>(req.dataLen);

  // if writing more than the size of the buf
  if (req.offset + amt_to_write > get_attrs().size) {
    truncate(req.offset + amt_to_write);
  }

  rAssert(req.offset + amt_to_write <= get_attrs().size);

  memcpy(&buf[req.offset], req.data, req.dataLen);
}

void MemFileIO::truncate(fs_off_t size) {
  rAssert(size >= 0);
  buf.resize(size);
}

bool MemFileIO::isWritable() const { return true; }

void MemFileIO::sync(bool /*dataSync*/) {}

}  // namespace encfs


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

#include "fs/MemBlockFileIO.h"

#include "base/logging.h"

#include "fs/MemFileIO.h"

namespace encfs {

static Interface MemBlockFileIO_iface =
    makeInterface("FileIO/MemBlock", 1, 0, 0);

MemBlockFileIO::MemBlockFileIO(int blockSize, const FSConfigPtr& cfg)
    : BlockFileIO(blockSize, cfg), impl(new MemFileIO(0)) {}

MemBlockFileIO::~MemBlockFileIO() {}

Interface MemBlockFileIO::interface() const { return MemBlockFileIO_iface; }

void MemBlockFileIO::setFileName(const char* name) {
  return impl->setFileName(name);
}

const char* MemBlockFileIO::getFileName() const { return impl->getFileName(); }

FsFileAttrs MemBlockFileIO::get_attrs() const { return impl->get_attrs(); }

ssize_t MemBlockFileIO::readOneBlock(const IORequest& req) const {
  try {
    return impl->read(req);
  }
  catch (...) {
    return -1;
  }
}

bool MemBlockFileIO::writeOneBlock(const IORequest& req) {
  try {
    impl->write(req);
    return true;
  }
  catch (...) {
    return false;
  }
}

void MemBlockFileIO::truncate(fs_off_t size) { return impl->truncate(size); }

bool MemBlockFileIO::isWritable() const { return impl->isWritable(); }

void MemBlockFileIO::sync(bool dataSync) { return impl->sync(dataSync); }

}  // namespace encfs

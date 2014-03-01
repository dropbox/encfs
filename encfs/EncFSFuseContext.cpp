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

#include <memory>

#include "fs/FsIO.h"
#include "fs/FileUtils.h"

#include "encfs/EncFSFuseContext.h"

namespace encfs {

EncFSFuseContext::EncFSFuseContext(std::shared_ptr<EncFS_Args> args,
                                   std::shared_ptr<EncFS_Opts> opts,
                                   std::shared_ptr<FsIO> fs)
    : _args(args), _opts(opts), _fs(fs) {}

size_t EncFSFuseContext::getAndResetUsageCounter() {
  auto count = _usage;
  _usage = 0;
  return count;
}

bool EncFSFuseContext::isMounted() { return (bool)_fs; }

size_t EncFSFuseContext::openFileCount() { return _openFileCount; }

void EncFSFuseContext::setRunning(bool running) { _running = running; }

bool EncFSFuseContext::isRunning() { return _running; }

bool EncFSFuseContext::isPublic() { return _args->isPublic; }

std::shared_ptr<FsIO> EncFSFuseContext::getFS() {
  _usage += 1;
  return _fs;
}

static void assertPath(const char * /*path*/, File * /*fptr*/) {
  // TODO: implement this for extra safety
}

void EncFSFuseContext::saveFile(struct fuse_file_info *fi, File f) {
  _openFileCount += 1;
  File *fptr = new File(std::move(f));
  fi->fh = (uint64_t)(uintptr_t)fptr;
}

File &EncFSFuseContext::getFile(const char *path, struct fuse_file_info *fi) {
  auto fptr = (File *)(uintptr_t)fi->fh;
  assertPath(path, fptr);
  return *fptr;
}

File EncFSFuseContext::releaseFile(const char *path,
                                   struct fuse_file_info *fi) {
  auto fptr = (File *)(uintptr_t)fi->fh;
  assertPath(path, fptr);
  File toret = std::move(*fptr);
  delete fptr;
  _openFileCount -= 1;
  return std::move(toret);
}

void EncFSFuseContext::unmountFS() { _fs = nullptr; }

std::shared_ptr<EncFS_Args> EncFSFuseContext::getArgs() { return _args; }
}

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

#ifndef _EncFSFuseContext_incl_
#define _EncFSFuseContext_incl_

#include <fuse.h>

#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

#include "encfs/EncFS_Args.h"
#include "fs/FsIO.h"
#include "fs/FileUtils.h"

namespace encfs {

struct EncFS_Args;

class EncFSFuseContext {
  std::shared_ptr<EncFS_Args> _args;
  std::shared_ptr<EncFS_Opts> _opts;
  std::shared_ptr<FsIO> _fs;
  size_t _openFileCount;
  size_t _usage;

  // for idle monitor
  bool _running;

  std::mutex contextMutex;

 public:
  EncFSFuseContext(std::shared_ptr<EncFS_Args> args,
                   std::shared_ptr<EncFS_Opts> opts, std::shared_ptr<FsIO> fs);

  std::mutex wakeupMutex;
  std::condition_variable wakeupCond;
  std::shared_ptr<std::thread> monitorThread;

  size_t getAndResetUsageCounter();
  bool isMounted();
  size_t openFileCount();
  void setRunning(bool running);
  bool isRunning();
  bool isPublic();
  std::shared_ptr<FsIO> getFS();
  void unmountFS();
  std::shared_ptr<EncFS_Args> getArgs();

  void saveFile(struct fuse_file_info *fi, File f);
  File &getFile(const char *path, struct fuse_file_info *fi);
  File releaseFile(const char *path, struct fuse_file_info *fi);
};
}

#endif

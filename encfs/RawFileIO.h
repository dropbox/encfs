/*****************************************************************************
 * Author:   Valient Gough <vgough@pobox.com>
 *
 *****************************************************************************
 * Copyright (c) 2004, Valient Gough
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

#ifndef _RawFileIO_incl_
#define _RawFileIO_incl_

#include "fs/FileIO.h"

#include <string>

namespace encfs {

class RawFileIO : public FileIO {
 public:
  RawFileIO(const std::string &fileName);

  // can't copy RawFileIO unless we dup fd
  RawFileIO(const RawFileIO &) = delete;
  RawFileIO &operator=(const RawFileIO &) = delete;

  int open(int flags, mode_t mode);

  virtual ~RawFileIO() override;

  virtual Interface interface() const override;

  virtual FsFileAttrs get_attrs() const override;

  virtual size_t read(const IORequest &req) const override;
  virtual void write(const IORequest &req) override;

  virtual void truncate(fs_off_t size) override;

  virtual bool isWritable() const override;

  virtual void sync(bool datasync) override;

 protected:
  std::string name;

  int fd;
  bool canWrite;
};

FsFileAttrs stat_to_fs_file_attrs(const struct stat &fs);

}  // namespace encfs

#endif

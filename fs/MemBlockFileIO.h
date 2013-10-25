
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

#ifndef _MEMBLOCKFILEIO_incl_
#define _MEMBLOCKFILEIO_incl_

#include "fs/BlockFileIO.h"

#include <string>
#include <vector>

namespace encfs {

class MemFileIO;

class MemBlockFileIO : public BlockFileIO {
 public:
  MemBlockFileIO(int blockSize, const FSConfigPtr &cfg);

  void setFileName(const char *name);
  const char *getFileName() const;

  virtual ~MemBlockFileIO();

  virtual Interface interface() const override;

  virtual FsFileAttrs get_attrs() const override;

  virtual bool isWritable() const;

  virtual void truncate(fs_off_t size) override;
  virtual void sync(bool dataSync) override;

 protected:
  virtual ssize_t readOneBlock(const IORequest &req) const override;
  virtual bool writeOneBlock(const IORequest &req) override;

 private:
  MemFileIO *impl;
};

}  // namespace encfs

#endif

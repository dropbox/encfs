/*****************************************************************************
 * Author:   Valient Gough <vgough@pobox.com>
 *
 *****************************************************************************
 * Copyright (c) 2003, Valient Gough
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

#ifndef _FileNode_incl_
#define _FileNode_incl_

#include <cstdint>

#include <memory>
#include <string>

#include "base/Mutex.h"
#include "base/optional.h"

#include "cipher/CipherKey.h"

#include "fs/Context.h"
#include "fs/CipherFileIO.h"
#include "fs/NameIO.h"
#include "fs/FileUtils.h"

namespace encfs {

class Cipher;
class FileIO;
class DirNode;

class FileNode {
 public:
  FileNode(const std::shared_ptr<EncFS_Context> &ctx, const FSConfigPtr &cfg,
           Path plaintextName, Path cipherName);
  ~FileNode();

  FileNode(const FileNode &src) = delete;
  FileNode &operator=(const FileNode &src) = delete;

  const Path &plaintextName() const;
  const Path &cipherName() const;

  // if setIVFirst is true, then the IV is changed before the name is changed
  // (default).  The reverse is also supported for special cases..
  bool setName(opt::optional<Path> plaintextName,
               opt::optional<Path> cipherName, uint64_t iv,
               bool setIVFirst = true);

  // Returns < 0 on error (-errno)
  int open(bool requestWrite, bool create);

  void flush();

  // getAttr returns 0 on success, -errno on failure
  int getAttr(FsFileAttrs &stbuf) const;
  fs_off_t getSize() const;

  ssize_t read(fs_off_t offset, byte *data, size_t size) const;
  bool write(fs_off_t offset, const byte *data, size_t size);

  // truncate the file to a particular size
  int truncate(fs_off_t size);

  // datasync or full sync
  int sync(bool dataSync);

 private:
  // doing locking at the FileNode level isn't as efficient as at the
  // lowest level of RawFileIO, since that means locks are held longer
  // (held during CPU intensive crypto operations!).  However it makes it
  // easier to avoid any race conditions with operations such as
  // truncate() which may result in multiple calls down to the FileIO
  // level.
  mutable Mutex mutex;

  FSConfigPtr fsConfig;

  std::shared_ptr<FileIO> io;
  std::shared_ptr<CipherFileIO> cipher_io;
  Path _pname;  // plaintext name
  Path _cname;  // encrypted name
  std::shared_ptr<EncFS_Context> _ctx;

  bool _setIV(uint64_t iv);
  int _unlocked_open(bool requestWrite, bool create);
};

}  // namespace encfs

#endif

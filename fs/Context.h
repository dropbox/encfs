/*****************************************************************************
 * Author:   Valient Gough <vgough@pobox.com>
 *
 *****************************************************************************
 * Copyright (c) 2007, Valient Gough
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

#ifndef _Context_incl_
#define _Context_incl_

#include <memory>
#include <set>
#include <string>
#include <unordered_map>

#include "base/config.h"
#include "base/Mutex.h"

namespace encfs {

class FileNode;
class DirNode;

class EncFS_Context {
 public:
  int openFileCount() const;

  std::shared_ptr<FileNode> lookupNode(const char *path) const;
  void trackNode(const char *path, const std::shared_ptr<FileNode> &node);
  void eraseNode(const char *path);
  void renameNode(const char *oldName, const char *newName);

  void setRoot(const std::shared_ptr<DirNode> &root);
  std::shared_ptr<DirNode> getRoot() const;
  bool isMounted() const;

 private:
  // set of open files, indexed by path
  typedef std::unordered_map<std::string, std::weak_ptr<FileNode> > FileMap;
  FileMap openFiles;

  std::shared_ptr<DirNode> root;
};

}  // namespace encfs

#endif

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

#include "fs/Context.h"

#include "fs/FileNode.h"
#include "fs/FileUtils.h"
#include "fs/DirNode.h"

#include "base/Error.h"

#include <iostream>
#include <string>
#include <stdexcept>

using std::shared_ptr;

namespace encfs {

shared_ptr<DirNode> EncFS_Context::getRoot() const { return root; }

void EncFS_Context::setRoot(const shared_ptr<DirNode> &r) { root = r; }

bool EncFS_Context::isMounted() const { return (bool)root; }

int EncFS_Context::openFileCount() const { return openFiles.size(); }

shared_ptr<FileNode> EncFS_Context::lookupNode(const char *path) const {
  try {
    const std::weak_ptr<FileNode> &ref = openFiles.at(std::string(path));
    return ref.lock();
  }
  catch (const std::out_of_range &err) {
    return nullptr;
  }
}

void EncFS_Context::renameNode(const char *from, const char *to) {
  auto from_path = std::string(from);
  auto to_path = std::string(to);

  assert(!openFiles.count(to_path));
  assert(openFiles.count(from_path));

  auto it = openFiles.find(from_path);
  openFiles[to_path] = it->second;
  openFiles.erase(it);
}

void EncFS_Context::trackNode(const char *cpath,
                              const shared_ptr<FileNode> &node) {
  auto path = std::string(cpath);
  assert(!openFiles.count(path));
  openFiles[std::move(path)] = std::weak_ptr<FileNode>(node);
}

void EncFS_Context::eraseNode(const char *path) {
  assert(openFiles.count(path));
  openFiles.erase(std::string(path));
}

}  // namespace encfs

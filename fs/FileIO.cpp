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

#include "fs/FileIO.h"

namespace encfs {

// create a custom errno category that returns the right
// error condition in default_error_condition
// (using generic_category to match with errc, not every implementation of
//  system_category does: (required by the C++11 standard, 19.5.1.5p4)
//  system_category().default_error_condition(errno).category ==
// generic_category() )
class errno_error_category : public std::error_category {
 public:
  errno_error_category() {}

  std::error_condition default_error_condition(int __i) const noexcept {
    return std::make_error_condition(static_cast<std::errc>(__i));
  }

  virtual const char *name() const noexcept { return "errno_error"; }
  virtual std::string message(int cond) const { return strerror(cond); }
};

const std::error_category &errno_category() noexcept {
  static const errno_error_category errno_category_instance;
  return errno_category_instance;
}

std::system_error create_errno_system_error(int e) {
  return std::system_error(e, errno_category());
}

std::system_error create_errno_system_error(std::errc e) {
  return create_errno_system_error((int)e);
}

FileIO::~FileIO() {}

}  // namespace encfs

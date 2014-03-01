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

#ifndef _ProgramPasswordReader_incl_
#define _ProgramPasswordReader_incl_

#include <string>

#include "fs/PasswordReader.h"

namespace encfs {

class ProgramPasswordReader : public PasswordReader {
 private:
  std::string passProg;
  std::string rootDir;

 public:
  ProgramPasswordReader(std::string passProg_, std::string rootDir_)
      : passProg(std::move(passProg_)), rootDir(std::move(rootDir_)) {}

  virtual SecureMem *readPassword(size_t maxLen, bool newPass) override;
};
}

#endif

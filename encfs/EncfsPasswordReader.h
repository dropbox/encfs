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

#ifndef _EncfsPasswordReader_incl_
#define _EncfsPasswordReader_incl_

#include <string>

#include "base/optional.h"

#include "fs/PasswordReader.h"

#include "encfs/ProgramPasswordReader.h"
#include "encfs/PromptPasswordReader.h"
#include "encfs/StdinPasswordReader.h"

namespace encfs {

class EncfsPasswordReader : public PasswordReader {
 private:
  bool useStdin;
  StdinPasswordReader stdinPasswordReader;
  opt::optional<ProgramPasswordReader> optProgramPasswordReader;
  PromptPasswordReader promptPasswordReader;

 public:
  EncfsPasswordReader(bool useStdin_, std::string passProg_,
                      std::string rootDir_)
      : useStdin(std::move(useStdin_)),
        optProgramPasswordReader(
            passProg_.empty()
                ? opt::nullopt
                : opt::make_optional(ProgramPasswordReader(
                      std::move(passProg_), std::move(rootDir_)))) {}

  EncfsPasswordReader(bool useStdin_) : useStdin(std::move(useStdin_)) {}

  virtual SecureMem *readPassword(size_t maxLen, bool newPass) override;
};
}

#endif

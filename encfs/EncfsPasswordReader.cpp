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

#include "encfs/StdinPasswordReader.h"
#include "encfs/ProgramPasswordReader.h"
#include "encfs/PromptPasswordReader.h"

#include "encfs/EncfsPasswordReader.h"

namespace encfs {

SecureMem *EncfsPasswordReader::readPassword(size_t maxLen, bool newPass) {
  PasswordReader *passwordReader;

  if (useStdin)
    passwordReader = &stdinPasswordReader;
  else if (newPass && optProgramPasswordReader)
    passwordReader = &*optProgramPasswordReader;
  else
    passwordReader = &promptPasswordReader;

  return passwordReader->readPassword(maxLen, newPass);
}
}

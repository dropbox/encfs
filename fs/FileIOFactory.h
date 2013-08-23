/*****************************************************************************
 * Author:   Rian Hunter <rian@alum.mit.edu>
 *
 *****************************************************************************
 * Copyright (c) 2013 Rian Hunter
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

#ifndef _FileIOFactory_incl_
#define _FileIOFactory_incl_

#include <string>

#include "fs/FileIO.h"

namespace encfs {

class FileIOFactory
{
public:
  virtual FileIO *createFileIO( const std::string &fileName ) = 0;
};

template <typename T>
class TemplateFileIOFactory : public FileIOFactory
{
 public:
  FileIO *createFileIO( const std::string &fileName ) override
  {
    return new T( fileName );
  }
};

}  // namespace encfs

#endif

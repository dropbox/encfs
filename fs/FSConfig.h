/*****************************************************************************
 * Author:   Valient Gough <vgough@pobox.com>
 *
 *****************************************************************************
 * Copyright (c) 2010 Valient Gough
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

#ifndef _FSConfig_incl_
#define _FSConfig_incl_

#include <memory>

#include "base/Interface.h"
#include "cipher/CipherKey.h"
#include "fs/fsconfig.pb.h"
#include "fs/PasswordReader.h"

#include <vector>

namespace encfs {

enum ConfigType {
  Config_None = 0,
  Config_Prehistoric,
  Config_V3 = 3,
  Config_V4 = 4,
  Config_V5 = 5,
  Config_V6 = 6,
  Config_V7 = 7
};

struct EncFS_Opts;
class CipherV1;
class NameIO;

CipherKey getUserKey(const EncfsConfig &config,
                     std::shared_ptr<PasswordReader> passwordReader);
CipherKey getNewUserKey(EncfsConfig &config,
                        std::shared_ptr<PasswordReader> passwordReader);

std::shared_ptr<CipherV1> getCipher(const EncfsConfig &cfg);
std::shared_ptr<CipherV1> getCipher(const Interface &iface, int keySize);

// helpers for serializing to/from a stream
std::ostream &operator<<(std::ostream &os, const EncfsConfig &cfg);
std::istream &operator>>(std::istream &os, EncfsConfig &cfg);

// Filesystem state
struct FSConfig {
  std::shared_ptr<EncfsConfig> config;
  std::shared_ptr<EncFS_Opts> opts;

  std::shared_ptr<CipherV1> cipher;
  CipherKey key;
  std::shared_ptr<NameIO> nameCoding;

  bool forceDecode;        // force decode on MAC block failures
  bool reverseEncryption;  // reverse encryption operation

  FSConfig() : forceDecode(false), reverseEncryption(false) {}
};

typedef std::shared_ptr<FSConfig> FSConfigPtr;

}  // namespace encfs

#endif

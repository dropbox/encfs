/*****************************************************************************
 * Author:   Valient Gough <vgough@pobox.com>
 *
 *****************************************************************************
 * Copyright (c) 2004-2013, Valient Gough
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

#include "base/ConfigReader.h"

#include "base/logging.h"

#include <fstream>
#include <sstream>

#include <cstring>

#include "base/types.h"

using std::make_pair;
using std::map;
using std::string;

namespace encfs {

ConfigReader::ConfigReader() {}

ConfigReader::~ConfigReader() {}

// read the entire file into a ConfigVar instance and then use that to decode
// into mapped variables.
bool ConfigReader::load(const char *fileName) {
  std::fstream f(fileName, std::ios::binary);
  if (!f) return false;

  std::ostringstream os;
  os << f.rdbuf();
  if (!f.eof()) return false;

  auto str = os.str();

  ConfigVar in;
  in.write((byte *)str.data(), str.size());

  return loadFromVar(in);
}

bool ConfigReader::loadFromVar(ConfigVar &in) {
  in.resetOffset();

  // parse.
  int numEntries = in.readInt();

  for (int i = 0; i < numEntries; ++i) {
    string key, value;
    in >> key >> value;

    if (key.length() == 0) {
      LOG(LERROR) << "Invalid key encoding in buffer";
      return false;
    }
    ConfigVar newVar(value);
    vars.insert(make_pair(key, newVar));
  }

  return true;
}

ConfigVar ConfigReader::operator[](const std::string &varName) const {
  // read only
  map<string, ConfigVar>::const_iterator it = vars.find(varName);
  if (it == vars.end())
    return ConfigVar();
  else
    return it->second;
}

ConfigVar &ConfigReader::operator[](const std::string &varName) {
  return vars[varName];
}

}  // namespace encfs

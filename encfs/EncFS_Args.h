/*****************************************************************************
 * Author:   Valient Gough <vgough@pobox.com>
 * Author:   Rian Hunter <rian@alum.mit.edu>
 *
 *****************************************************************************
 * Copyright (c) 2003-2004, Valient Gough
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

#ifndef _EncFS_Args_incl_
#define _EncFS_Args_incl_

#include <memory>
#include <sstream>
#include <string>

#include "fs/FileUtils.h"

namespace encfs {

// Maximum number of arguments that we're going to pass on to fuse.  Doesn't
// affect how many arguments we can handle, just how many we can pass on..
const int MaxFuseArgs = 32;
struct EncFS_Args {
  std::string mountPoint;       // where to make filesystem visible
  bool isDaemon;                // true == spawn in background, log to syslog
  bool isThreaded;              // true == threaded
  bool isVerbose;               // false == only enable warning/error messages
  int idleTimeout;              // 0 == idle time in minutes to trigger unmount
  bool useStdin;                // true == accept password from stdin
  std::string passwordProgram;  // program to execute to get filesystem password
  const char *fuseArgv[MaxFuseArgs];
  int fuseArgc;
  bool isPublic;
  bool mountOnDemand;

  std::shared_ptr<EncFS_Opts> opts;

  // for debugging
  // In case someone sends me a log dump, I want to know how what options are
  // in effect.  Not internationalized, since it is something that is mostly
  // useful for me!
  std::string toString() {
    std::ostringstream ss;
    ss << (isDaemon ? "(daemon) " : "(fg) ");
    ss << (isThreaded ? "(threaded) " : "(UP) ");
    if (idleTimeout > 0) ss << "(timeout " << idleTimeout << ") ";
    if (opts->checkKey) ss << "(keyCheck) ";
    if (opts->forceDecode) ss << "(forceDecode) ";
    if (useStdin) ss << "(useStdin) ";
    if (opts->annotate) ss << "(annotate) ";
    if (opts->reverseEncryption) ss << "(reverseEncryption) ";
    if (isPublic) ss << "(public) ";
    if (mountOnDemand) ss << "(mountOnDemand) ";
    if (opts->delayMount) ss << "(delayMount) ";
    for (int i = 0; i < fuseArgc; ++i) ss << fuseArgv[i] << ' ';

    return ss.str();
  }

  EncFS_Args()
      : isDaemon(false),
        isThreaded(false),
        isVerbose(false),
        idleTimeout(0),
        fuseArgc(0),
        opts(std::make_shared<EncFS_Opts>()) {
    for (int i = 0; i < MaxFuseArgs; ++i) {
      fuseArgv[i] = nullptr;
    }
  }
};
}

#endif

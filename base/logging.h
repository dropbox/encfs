/*****************************************************************************
 * Author:   Rian Hunter <rian@alum.mit.edu>
 *
 *****************************************************************************
 * Copyright (c) 2013, Rian Hunter
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

#ifndef _encfs_logging_incl_
#define _encfs_logging_incl_

#include <assert.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  ENCFS_LOG_NEVER,
  ENCFS_LOG_DEBUG,
  ENCFS_LOG_INFO,
  ENCFS_LOG_WARNING,
  ENCFS_LOG_ERROR,
  ENCFS_LOG_NOTHING,
} EncfsLogLevel;

#ifndef _IS_LOGGING_CPP
extern EncfsLogLevel _CUR_LEVEL;
#else
EncfsLogLevel _CUR_LEVEL = ENCFS_LOG_DEBUG;
#endif

void
encfs_log_print(const char *toprint);

void
encfs_set_log_level(EncfsLogLevel level);

#ifdef __cplusplus
}
#endif

// define special C++ style logger
#ifdef __cplusplus

#include <string>
#include <sstream>

namespace encfs {

typedef EncfsLogLevel LogLevel;
const LogLevel NEVER = ENCFS_LOG_NEVER;
const LogLevel DEBUG = ENCFS_LOG_DEBUG;
const LogLevel INFO = ENCFS_LOG_INFO;
const LogLevel WARNING = ENCFS_LOG_WARNING;
const LogLevel LERROR = ENCFS_LOG_ERROR;
const LogLevel NOTHING = ENCFS_LOG_NOTHING;

inline
void
_cond_print(LogLevel level, const char *p) {
  if (level >= _CUR_LEVEL) encfs_log_print(p);
}

class Logger {
public:
  LogLevel _level;

  Logger(LogLevel level) :
    _level(level) {}

  ~Logger() {
    _cond_print(_level, "\n");
  }

  template<class T>
  const encfs::Logger &
  operator<<(const T & dl) const {
    std::ostringstream os;
    os << dl;
    encfs::_cond_print(_level, os.str().c_str());
    return *this;
  }

  const encfs::Logger &
  operator<<(const char *a) const {
    encfs::_cond_print(_level, a);
    return *this;
  }
};

// TODO: this potentially should be a macro to save the file and line number
inline
Logger
LOG(LogLevel level) {
  return Logger(level);
}

inline
Logger
LOG_IF(LogLevel level, bool should_log) {
  return LOG(should_log ? level : NEVER);
}

inline
Logger
CHECK(bool should_log) {
  return LOG_IF(WARNING, should_log);
}

}


#endif

#endif

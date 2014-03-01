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
} encfs_log_level_t;

typedef void (*encfs_log_printer_t)(const char *, int, encfs_log_level_t,
                                    const char *);

extern encfs_log_level_t _CUR_LEVEL;
extern encfs_log_printer_t _LOG_PRINTER;

void encfs_set_log_printer(encfs_log_printer_t printer);

void encfs_set_log_level(encfs_log_level_t level);

#ifdef __cplusplus
}
#endif

// define special C++ style logger
#ifdef __cplusplus

#include <string>
#include <sstream>

namespace encfs {

typedef encfs_log_level_t LogLevel;
const LogLevel NEVER = ENCFS_LOG_NEVER;
const LogLevel DEBUG = ENCFS_LOG_DEBUG;
const LogLevel INFO = ENCFS_LOG_INFO;
const LogLevel WARNING = ENCFS_LOG_WARNING;
const LogLevel LERROR = ENCFS_LOG_ERROR;
const LogLevel NOTHING = ENCFS_LOG_NOTHING;

class Logger {
 public:
  const char *_filename;
  int _lineno;
  LogLevel _level;
  std::ostringstream _os;

  Logger(const char *filename, int lineno, LogLevel level)
      : _filename(filename), _lineno(lineno), _level(level) {}

  ~Logger() {
    if (_level >= _CUR_LEVEL)
      _LOG_PRINTER(_filename, _lineno, _level, _os.str().c_str());
  }

  template <class T>
  encfs::Logger &operator<<(T &&dl) {
    _os << std::forward<T>(dl);
    return *this;
  }
};

#define LOG(level) Logger(__FILE__, __LINE__, level)
#define LOG_IF(level, should_log) LOG(should_log ? level : NEVER)
#define CHECK(should_log) LOG_IF(WARNING, should_log)
}

#endif

#endif

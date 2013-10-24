/*****************************************************************************
 * Author:   Valient Gough <vgough@pobox.com>
 *
 *****************************************************************************
 * Copyright (c) 2003, Valient Gough
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

#ifndef _MutexPthreads_incl_
#define _MutexPthreads_incl_

#include <stdexcept>

#include <pthread.h>

namespace encfs {

class MutexPthreads {
 public:
  pthread_mutex_t _mutex;
  MutexPthreads() { pthread_mutex_init(&_mutex, 0); }

  ~MutexPthreads() { pthread_mutex_destroy(&_mutex); }

  void lock() {
    const int ret = pthread_mutex_lock(&_mutex);
    if (ret) throw std::runtime_error("could not lock mutex");
  }

  void unlock() {
    const int ret = pthread_mutex_unlock(&_mutex);
    if (ret) throw std::runtime_error("could not lock mutex");
  }
};

}  // namespace encfs

#endif

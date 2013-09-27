/*****************************************************************************
 * Author:   Valient Gough <vgough@pobox.com>
 *
 *****************************************************************************
 * Copyright (c) 2004, Valient Gough
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

#ifndef _FileIO_incl_
#define _FileIO_incl_

#include <memory>
#include <system_error>

#include "base/Interface.h"
#include "base/types.h"

#include "fs/encfs.h"
#include "fs/fstypes.h"

namespace encfs {

const std::error_category &errno_category() noexcept;

static inline int get_errno_or_default(const std::system_error &err, int nomatch)
{
  if(err.code().default_error_condition().category() == std::generic_category()) return err.code().value();
  return nomatch;
}

static inline int get_errno_or_abort(const std::system_error &err)
{
  if(err.code().default_error_condition().category() == std::generic_category()) return err.code().value();
  throw err;
}

template<typename T, typename R, typename... Args>
std::function<R(Args...)> bindMethod(const std::shared_ptr<T> &obj, R (T::*fn)(Args...))
{
  return [=](Args... args) {
    return (obj.get()->*fn)( args... );
  };
}

template<typename T, typename R, typename... Args>
std::function<R(Args...)> bindMethod(const std::shared_ptr<T> &obj, R (T::*fn)(Args...) const)
{
  return [=](Args... args) {
    return (obj.get()->*fn)( args... );
  };
}

template<typename T, typename R, typename... Args>
std::function<R(Args...)> bindMethod(T *obj, R (T::*fn)(Args...))
{
  return [=](Args... args) {
    return (obj->*fn)( args... );
  };
}

template<typename R, typename F, typename... Args, typename std::enable_if<!std::is_void<R>::value, int>::type = 0>
int withExceptionCatcher(int defaultRes, F fn, R *res, Args... args)
{
  try
  {
    if (res) *res = fn( args... );
    else fn( args... );
    return 0;
  } catch( const std::system_error &err )
  {
    return -get_errno_or_default( err, defaultRes );
  } catch ( ... )
  {
    return -defaultRes;
  }
}

template<typename F, typename... Args>
int withExceptionCatcher(int defaultRes, F fn, Args... args)
{
  try
  {
    fn( args... );
    return 0;
  } catch( const std::system_error &err )
  {
    return -get_errno_or_default( err, defaultRes );
  } catch ( ... )
  {
    return -defaultRes;
  }
}

template<typename R, typename... Args, typename std::enable_if<!std::is_void<R>::value, int>::type = 0>
std::function<int(R *, Args...)> wrapWithExceptionCatcher(int defaultRes, std::function<R(Args...)> fn)
{
  return [=] (Args... args, R *res) {
    return withExceptionCatcher( defaultRes, fn, res, args... );
  };
}

template<typename... Args>
std::function<int(Args...)> wrapWithExceptionCatcher(int defaultRes, std::function<void(Args...)> fn)
{
  return [=] (Args... args) {
    return withExceptionCatcher( defaultRes, fn, args... );
  };
}

template<typename F, typename R, typename... Args>
std::function<int(R *, Args...)> wrapWithExceptionCatcher(int defaultRes, F fn)
{
  return [=] (Args... args, R *res) {
    return withExceptionCatcher( defaultRes, fn, args..., res );
  };
}

struct IORequest
{
    fs_off_t offset;

    // amount of bytes to read/write.
    byte *data;
    size_t dataLen;

    IORequest(fs_off_t offset_, byte *data_, size_t len_)
    : offset(offset_)
    , data(data_)
    , dataLen(len_)
    {}

    IORequest() : IORequest(0, nullptr, 0)
    {}
};

class FileIO
{
public:
    virtual ~FileIO() =0;

    virtual Interface interface() const =0;

    virtual FsFileAttrs get_attrs() const =0;

    virtual size_t read(const IORequest &req) const =0;
    virtual void write(const IORequest &req) =0;

    virtual void truncate(fs_off_t size) =0;

    virtual bool isWritable() const =0;

    virtual void sync(bool datasync) const =0;
};

}  // namespace encfs

#endif


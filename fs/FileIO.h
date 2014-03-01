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

#include "fs/fstypes.h"

namespace encfs {

const std::error_category &errno_category() noexcept;

std::system_error create_errno_system_error(std::errc e);
std::system_error create_errno_system_error(int e);

static inline int get_errno_or_default(const std::system_error &err,
                                       int nomatch) {
  auto err_cond = err.code().default_error_condition();
  if (err_cond.category() == std::generic_category()) return err_cond.value();
  return nomatch;
}

static inline int get_errno_or_abort(const std::system_error &err) {
  auto err_cond = err.code().default_error_condition();
  if (err_cond.category() == std::generic_category()) return err_cond.value();
  throw err;
}

template <typename T, typename U, typename R, typename... Args,
          typename std::enable_if<std::is_convertible<T *, U *>::value &&
                                  !std::is_const<T>::value>::type * = nullptr>
std::function<R(Args...)> bindMethod(const std::shared_ptr<T> &obj,
                                     R (U::*fn)(Args...)) {
  struct {
    R (U::*fn)(Args...);
  } a = {fn};
  return [=](Args &&... args) {
    return (((U *)obj.get())->*a.fn)(std::forward<Args>(args)...);
  };
}

// need to have this template because there is no auto casting from T => const T
// for
// shared_ptr
template <typename T, typename U, typename R, typename... Args,
          typename std::enable_if<
              std::is_convertible<T *, U *>::value>::type * = nullptr>
std::function<R(Args...)> bindMethod(const std::shared_ptr<T> &obj,
                                     R (U::*fn)(Args...) const) {
  struct {
    R (U::*fn)(Args...) const;
  } a = {fn};
  return [=](Args &&... args) {
    return (((U *)obj.get())->*a.fn)(std::forward<Args>(args)...);
  };
}

template <typename T, typename U, typename R, typename... Args,
          typename std::enable_if<
              std::is_convertible<T *, U *>::value>::type * = nullptr>
std::function<R(Args...)> bindMethod(const std::shared_ptr<const T> &obj,
                                     R (U::*fn)(Args...) const) {
  struct {
    R (U::*fn)(Args...) const;
  } a = {fn};
  return [=](Args &&... args) {
    return (((U *)obj.get())->*a.fn)(std::forward<Args>(args)...);
  };
}

template <typename T, typename U, typename R, typename... Args,
          typename std::enable_if<
              std::is_convertible<T *, U *>::value>::type * = nullptr>
std::function<R(Args...)> bindMethod(T *obj, R (U::*fn)(Args...)) {
  // NB: workaround for bug in GCC 4.9, it can't
  //     close over bare PMF variables, so wrap it in a struct
  struct {
    R (U::*fn)(Args...);
  } a = {fn};
  return [=](Args &&... args) {
    return (((U *)obj)->*a.fn)(std::forward<Args>(args)...);
  };
}

template <typename T, typename U, typename R, typename... Args,
          typename std::enable_if<
              std::is_convertible<T *, U *>::value>::type * = nullptr>
std::function<R(Args...)> bindMethod(const T *obj, R (U::*fn)(Args...) const) {
  struct {
    R (U::*fn)(Args...) const;
  } a = {fn};
  return [=](Args &&... args) {
    return (((U *)obj)->*a.fn)(std::forward<Args>(args)...);
  };
}

template <typename T, typename U, typename R, typename... Args,
          typename std::enable_if<
              std::is_convertible<T *, U *>::value>::type * = nullptr>
std::function<R(Args...)> bindMethod(T &obj, R (U::*fn)(Args...)) {
  struct {
    R (U::*fn)(Args...);
  } a = {fn};
  return [=, &obj](Args &&... args) {
    return (((U *)&obj)->*a.fn)(std::forward<Args>(args)...);
  };
}

template <typename T, typename U, typename R, typename... Args,
          typename std::enable_if<
              std::is_convertible<T *, U *>::value>::type * = nullptr>
std::function<R(Args...)> bindMethod(const T &obj, R (U::*fn)(Args...) const) {
  // reference is only guaranteed to survive for the function invocation
  const U *p = &obj;
  struct {
    R (U::*fn)(Args...) const;
  } a = {fn};
  return [=](Args &&... args) {
    return (p->*a.fn)(std::forward<Args>(args)...);
  };
}

template <typename R, typename F, typename... Args,
          typename std::enable_if<
              std::is_convertible<F, std::function<R(Args...)>>::value &&
                  !std::is_void<R>::value,
              int>::type * = nullptr>
int withExceptionCatcher(int defaultRes, F fn, R *res, Args &&... args) {
  try {
    if (res)
      *res = fn(std::forward<Args>(args)...);
    else
      fn(std::forward<Args>(args)...);
    return 0;
  }
  catch (const std::system_error &err) {
    return -get_errno_or_default(err, defaultRes);
  }
  catch (...) {
    return -defaultRes;
  }
}

template <typename F, typename... Args>
int withExceptionCatcherNoRet(int defaultRes, F fn, Args &&... args) {
  try {
    fn(std::forward<Args>(args)...);
    return 0;
  }
  catch (const std::system_error &err) {
    return -get_errno_or_default(err, defaultRes);
  }
  catch (...) {
    return -defaultRes;
  }
}

template <typename R, typename... Args,
          typename std::enable_if<!std::is_void<R>::value, int>::type = 0>
std::function<int(R *, Args...)> wrapWithExceptionCatcher(
    int defaultRes, std::function<R(Args...)> fn) {
  return [=](R *res, Args &&... args) {
    return withExceptionCatcher(defaultRes, fn, res,
                                std::forward<Args>(args)...);
  };
}

template <typename... Args>
std::function<int(Args...)> wrapWithExceptionCatcher(
    int defaultRes, std::function<void(Args...)> fn) {
  return [=](Args &&... args) {
    return withExceptionCatcherNoRet(defaultRes, fn,
                                     std::forward<Args>(args)...);
  };
}

struct IORequest {
  fs_off_t offset;

  // amount of bytes to read/write.
  byte *data;
  size_t dataLen;

  IORequest(fs_off_t offset_, byte *data_, size_t len_)
      : offset(offset_), data(data_), dataLen(len_) {}

  IORequest() : IORequest(0, nullptr, 0) {}
};

class FileIO {
 public:
  virtual ~FileIO() = 0;

  virtual Interface interface() const = 0;

  virtual FsFileAttrs get_attrs() const = 0;

  virtual size_t read(const IORequest &req) const = 0;
  virtual void write(const IORequest &req) = 0;

  virtual void truncate(fs_off_t size) = 0;

  virtual bool isWritable() const = 0;

  virtual void sync(bool datasync) = 0;
};

}  // namespace encfs

#endif

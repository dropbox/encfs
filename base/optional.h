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

/* A lightweight version of C++1y optional */

#ifndef _optional_incl_
#define _optional_incl_

#include <new>
#include <stdexcept>
#include <type_traits>

//#include "base/config.h"

#ifdef HAVE_OPTIONAL
#include <optional>
namespace opt = std;
#else
namespace opt {

class nullopt_t {
 public:
  constexpr nullopt_t() {}
};

constexpr nullopt_t nullopt;

class bad_optional_access : public std::logic_error {
 public:
  bad_optional_access(const char *a) : std::logic_error(a) {}
};

template <class T,
          typename std::enable_if<!std::is_reference<T>::value, int>::type = 0>
class optional {
 private:
  union {
    char _null_state;
    T _val;
  };
  bool _engaged;

 public:
  constexpr optional() : _null_state('\0'), _engaged(false) {}

  constexpr optional(nullopt_t) : optional() {}

  constexpr optional(T val) : _val(std::move(val)), _engaged(true) {}

  optional &operator=(const optional &val) {
    if (val._engaged) {
      if (_engaged)
        _val = val._val;
      else {
        // construct in place
        new (&_val) T(val._val);
        _engaged = true;
      }
    } else {
      if (_engaged) {
        _val.~T();
        _engaged = false;
      }
    }
    return *this;
  }

  optional(const optional &val) : optional() { *this = val; }

  optional &operator=(optional &&val) {
    if (val._engaged) {
      if (_engaged)
        _val = std::move(val._val);
      else {
        // construct in place
        new (&_val) T(std::move(val._val));
        _engaged = true;
      }
    } else {
      if (_engaged) {
        _val.~T();
        _engaged = false;
      }
    }
    return *this;
  }

  optional(optional &&val) : optional() { *this = std::move(val); }

  ~optional() {
    if (_engaged) {
      _val.~T();
      _engaged = false;
    }
  }

  constexpr const T &operator*() const {
    //    static_assert( _engaged, "bad optional access");
    return _val;
  }

  T &operator*() {
    if (!_engaged) {
      throw bad_optional_access("bad optional access");
    }
    return _val;
  }

  constexpr const T *operator->() const {
    //    static_assert( _engaged, "bad optional access");
    return &_val;
  }

  T *operator->() {
    if (!_engaged) {
      throw bad_optional_access("bad optional access");
    }
    return &_val;
  }

  constexpr explicit operator bool() const { return _engaged; }
};

template <class T>
constexpr bool operator==(optional<T> f, nullopt_t) {
  return !f;
}

template <class T>
constexpr bool operator==(nullopt_t, optional<T> f) {
  return !f;
}

template <class T>
constexpr bool operator==(optional<T> a, optional<T> b) {
  return a && b ? *a == *b : !a && !b;
}

template <class T>
constexpr optional<typename std::decay<T>::type> make_optional(T &&value) {
  return optional<typename std::decay<T>::type>(std::forward<T>(value));
}
}

#endif

#endif

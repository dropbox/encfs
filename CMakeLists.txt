cmake_minimum_required(VERSION 2.8)
project(Encfs C CXX)

set (ENCFS_MAJOR 2)
set (ENCFS_MINOR 0)
set (ENCFS_VERSION "${ENCFS_MAJOR}.${ENCFS_MINOR}")

option (BUILD_SHARED_LIBS "Build dynamic link libraries" OFF)

option (WITH_OPENSSL "WithOpenSSL" OFF)
option (WITH_BOTAN "WithBotan" ON)
option (WITH_COMMON_CRYPTO "WithCommonCrypto" OFF)

include (CheckIncludeFileCXX)
check_include_file_cxx (CommonCrypto/CommonCryptor.h HAVE_COMMON_CRYPTOR_H)

if (HAVE_COMMON_CRYPTOR_H)
   set (WITH_COMMON_CRYPTO ON)
endif (HAVE_COMMON_CRYPTOR_H)

set (CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH}
    "${CMAKE_SOURCE_DIR}/CMakeModules/")

# Include Crypto checks here so that they can set values in config.h
if (WITH_COMMON_CRYPTO)
  set (WITH_BOTAN OFF)
  set (WITH_OPENSSL OFF)

  find_library (SECURITY_FRAMEWORK Security)
  mark_as_advanced (SECURITY_FRAMEWORK)
elseif (WITH_BOTAN)
  set (WITH_COMMON_CRYPTO OFF)
  set (WITH_OPENSSL OFF)

  find_package (Botan REQUIRED)
elseif (WITH_OPENSSL)
  set (WITH_BOTAN OFF)
  set (WITH_COMMON_CRYPTO OFF)

  find_package (OpenSSL REQUIRED)
  include (OpenSSLTests)
endif (WITH_COMMON_CRYPTO)

# Tweak compiler flags.
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Werror")

include (CheckCXXCompilerFlag)
check_cxx_compiler_flag (-std=c++11 HAVE_CXX11_FLAG)
check_cxx_compiler_flag (-std=c++0x HAVE_CXX0x_FLAG)

if (HAVE_CXX11_FLAG)
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
elseif (HAVE_CXX0x_FLAG)
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++0x")
endif (HAVE_CXX11_FLAG)

# Flume specific flags.
find_package (FUSE)

# Packaging config.
set (CPACK_PACKAGE_NAME "Encfs")
set (CPACK_PACKAGE_VERSION_MAJOR ${ENCFS_MAJOR})
set (CPACK_PACKAGE_VERSION_MINOR ${ENCFS_MINOR})
set (CPACK_SOURCE_GENERATOR TGZ)
set (CPACK_SOURCE_IGNORE_FILES
    "/_darcs/"
    "/build/")
include (CPack)

# Check for external files.
check_include_file_cxx (attr/xattr.h HAVE_ATTR_XATTR_H)
check_include_file_cxx (sys/xattr.h HAVE_SYS_XATTR_H)

check_include_file_cxx (valgrind/valgrind.h HAVE_VALGRIND_VALGRIND_H)
check_include_file_cxx (valgrind/memcheck.h HAVE_VALGRIND_MEMCHECK_H)

# Used with CommonCrypto
check_include_file_cxx (Security/SecRandom.h HAVE_SEC_RANDOM_H)

# Check if xattr functions take extra argument.
include (CheckCXXSourceCompiles)
CHECK_CXX_SOURCE_COMPILES ("#include <sys/types.h>
  #include <sys/xattr.h>
  int main() { getxattr(0,0,0,0,0,0); return 1; } " XATTR_ADD_OPT)

# Libraries or programs used for multiple modules.
find_package (Protobuf REQUIRED)
include_directories (${PROTOBUF_INCLUDE_DIR})

find_package (GFlags)
if (GFLAGS_FOUND)
    include_directories (${GFLAGS_INCLUDE_DIRS})
endif (GFLAGS_FOUND)

find_package (Threads)

set (CMAKE_THREAD_PREFER_PTHREAD)
find_program (POD2MAN pod2man)

find_package (GTest)
if (GTEST_FOUND) 
    include_directories(${GTEST_INCLUDE_DIR})
    enable_testing()
endif (GTEST_FOUND)

# Prefix for encfs module includes.
include_directories (${Encfs_BINARY_DIR})
include_directories (${Encfs_SOURCE_DIR})

# Subdirectories.
add_subdirectory(base)
add_subdirectory(cipher)
add_subdirectory(fs)
add_subdirectory(encfs)
add_subdirectory(util)
add_subdirectory(po)

# Test target.
if (GTEST_FOUND)
    add_custom_target (test COMMAND ${CMAKE_TEST_COMMAND} DEPENDS
        cipher/cipher-tests fs/fs-tests)
endif (GTEST_FOUND)



##  Copyright (c) 2016 The WebM project authors. All Rights Reserved.
##
##  Use of this source code is governed by a BSD-style license
##  that can be found in the LICENSE file in the root of the source
##  tree. An additional intellectual property rights grant can be found
##  in the file PATENTS.  All contributing project authors may
##  be found in the AUTHORS file in the root of the source tree.
cmake_minimum_required(VERSION 3.2)

if (CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang" OR
    CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR
    CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  CHECK_CXX_COMPILER_FLAG("-std=c++11" HAVE_CXX11)
  if (HAVE_CXX11)
    set(CMAKE_CXX_FLAGS "-std=c++11 ${CMAKE_CXX_FLAGS}" CACHE STRING "" FORCE)
  endif ()
endif ()

# C++11 compile tests.
if (MSVC OR HAVE_CXX11)
  # std::unique_ptr
  check_cxx_source_compiles("
      #include <memory>
      int main(int argc, const char* argv[]) {
        std::unique_ptr<int> ptr;
        (void)ptr;
        return 0;
      }"
      HAVE_UNIQUE_PTR)

  # default member values
  check_cxx_source_compiles("
      struct Foo {
        int a = 0;
      };
      int main(int argc, const char* argv[]) {
        Foo bar;
        (void)bar;
        return 0;
      }"
      HAVE_DEFAULT_MEMBER_VALUES)

  # defaulted methods
  check_cxx_source_compiles("
      struct Foo {
        Foo() = default;
        ~Foo() = default;
      };
      int main(int argc, const char* argv[]) {
        Foo bar;
        (void)bar;
        return 0;
      }"
      HAVE_DEFAULTED_MEMBER_FUNCTIONS)

  # deleted methods
  check_cxx_source_compiles("
      struct Foo {
        Foo() {}
        Foo(const Foo&) = delete;
      };
      int main(int argc, const char* argv[]) {
        Foo bar;
        (void)bar;
        return 0;
      }"
      HAVE_DELETED_MEMBER_FUNCTIONS)

  # auto&
  check_cxx_source_compiles("
      int main(int argc, const char* argv[]) {
        int a;
        auto& b = a;
        (void)b;
        return 0;
      }"
      HAVE_AUTO_REF)

  # ranged for
  check_cxx_source_compiles("
      int main(int argc, const char* argv[]) {
        int a[4];
        for (int& b : a) {
          b = 0;
        }
        return 0;
      }"
      HAVE_RANGED_FOR)
endif ()

if (NOT HAVE_UNIQUE_PTR
    OR NOT HAVE_DEFAULT_MEMBER_VALUES
    OR NOT HAVE_DEFAULTED_MEMBER_FUNCTIONS
    OR NOT HAVE_DELETED_MEMBER_FUNCTIONS
    OR NOT HAVE_AUTO_REF
    OR NOT HAVE_RANGED_FOR)
  # TODO(tomfinegan): Update settings at the include site instead of in here.
  set(ENABLE_TESTS OFF)
  set(ENABLE_WEBMTS OFF)
  message(WARNING "C++11 feature(s) not supported, tests and webmts disabled.")
endif ()


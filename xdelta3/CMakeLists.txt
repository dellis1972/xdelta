cmake_minimum_required(VERSION 3.6.0)

project(xdelta C)


option(ENABLE_NDK "Build with Android's NDK" OFF)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(BUILD_SHARED_LIBS OFF)
set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")

include(CheckIncludeFiles)
include(CheckCCompilerFlag)
include(CheckTypeSize)
include(FindLibLZMA)

macro(c_compiler_has_flag _flag)
  check_c_compiler_flag(-${_flag} HAS_${_flag}_C)
  if (HAS_${_flag}_C)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -${_flag}")
  endif()
endmacro(c_compiler_has_flag)

set(XDELTA_SOURCES
  xdelta3.c
  )

# gcc -DHAVE_CONFIG_H -I.    -Wall -Wshadow -fno-builtin -Wextra -Wsign-compare -Wextra -Wno-unused-parameter -Wno-unused-function -pedantic -std=c99 -DREGRESSION_TEST=1 -DSECONDARY_DJW=1 -DSECONDARY_FGK=1 -DXD3_MAIN=1  -DXD3_DEBUG=0 -g -O2 -MT xdelta3-xdelta3.o -MD -MP -MF .deps/xdelta3-xdelta3.Tpo -c -o xdelta3-xdelta3.o `test -f 'xdelta3.c' || echo './'`xdelta3

set(TEST_COMPILER_ARGS
  Wall
  Wshadow
  fno-builtin
  Wextra
  Wsign-compare
  Wextra
  Wno-unused-parameter
  Wno-unused-function
  pedantic
  fpic
  fPIC
  )

add_definitions("-DHAVE_CONFIG_H")
add_definitions("-DREGRESSION_TEST=0")
add_definitions("-DSECONDARY_DJW=1")
add_definitions("-DSECONDARY_FGK=1")
add_definitions("-DXD3_MAIN=1")
add_definitions("-DXD3_DEBUG=0")
add_definitions("-DEXTERNAL_COMPRESSION=0")

if(ENABLE_NDK)
  add_definitions("-DPLATFORM_ANDROID")
  add_definitions("-DANDROID")
  add_definitions("-DLINUX -Dlinux -D__linux__")
else()
  # MinGW/mxe need it for {v,a}sprintf
  add_definitions("-D_GNU_SOURCE")
  if(APPLE)
    add_definitions("-DAPPLE_OS_X")
    set(HOST_BUILD_NAME "host-Darwin")
  elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL Linux)
    if(NOT MINGW AND NOT WIN32)
      add_definitions("-DLINUX -Dlinux -D__linux__")
      set(HOST_BUILD_NAME "host-Linux")
    endif()
  endif()

  if(MINGW_TARGET_32)
    include_directories("${MINGW_DEPENDENCIES_ROOT_DIR}/x86/include")
    link_directories("${MINGW_DEPENDENCIES_ROOT_DIR}/x86/lib")
  endif()

  if(MINGW_TARGET_64)
    include_directories("${MINGW_DEPENDENCIES_ROOT_DIR}/x86_64/include")
    link_directories("${MINGW_DEPENDENCIES_ROOT_DIR}/x86_64/lib")
  endif()

  if(UNIX)
    set(EXTRA_COMPILER_FLAGS "${EXTRA_COMPILER_FLAGS} -Wa,--noexecstack")
    set(EXTRA_LINKER_FLAGS "${EXTRA_LINKER_FLAGS} -shared -fpic")
  endif()

  if(WIN32 OR MINGW)
    message(STATUS "Win32 or MinGW")
    set(EXTRA_COMPILER_FLAGS "${EXTRA_COMPILER_FLAGS} -DWINDOWS -DNTDDI_VERSION=NTDDI_VISTA -D_WIN32_WINNT=_WIN32_WINNT_VISTA -fomit-frame-pointer")
    set(EXTRA_LINKER_FLAGS "${EXTRA_LINKER_FLAGS} -ldl -lmman -pthread -lwsock32 -lshlwapi -lpsapi")

    if (MINGW_TARGET_32)
      set(ANDROID_ABI "host-mxe-Win32")
    elseif (MINGW_TARGET_64)
      set(ANDROID_ABI "host-mxe-Win64")
    endif()
  endif()

  if(DEFINED HOST_BUILD_NAME)
    include_directories("../../bin/${CMAKE_BUILD_TYPE}/include/${HOST_BUILD_NAME}")
    include_directories("../../bin/${CMAKE_BUILD_TYPE}/include/${HOST_BUILD_NAME}/eglib")

    if (DEFINED CONFIGURATION)
      include_directories("../../bin/${CONFIGURATION}/include/${HOST_BUILD_NAME}")
      include_directories("../../bin/${CONFIGURATION}/include/${HOST_BUILD_NAME}/eglib")
    endif()
  endif()
endif()

include_directories("${PROJECT_SOURCE_DIR}")
include_directories("${PROJECT_BINARY_DIR}")

check_type_size("size_t" SIZEOF_SIZE_T)
check_type_size("unsigned int" SIZEOF_UNSIGNED_INT)
check_type_size("unsigned long" SIZEOF_UNSIGNED_LONG)
check_type_size("unsigned long long" SIZEOF_UNSIGNED_LONG_LONG)

check_include_files("lzma.h" HAVE_LZMA_H)

foreach(arg ${TEST_COMPILER_ARGS})
  c_compiler_has_flag(${arg})
endforeach(arg)

configure_file(
  "${PROJECT_SOURCE_DIR}/cmake-config.h.in"
  "${PROJECT_BINARY_DIR}/config.h"
  )

add_executable(xdelta3 ${XDELTA_SOURCES})
if(NOT APPLE)
  target_link_libraries(xdelta3 -static)
endif()

if(LIBLZMA_FOUND)
  set(HAVE_LIBLZMA "1")
  target_link_libraries(xdelta3 "${LIBLZMA_LIBRARIES}")
  include_directories("${LIBLZMA_INCLUDE_DIRS}")
endif()

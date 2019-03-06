#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT([libzidx], [1.0.0-dev], [omer AT utdallas DOT edu])
AC_LANG([C])
AC_CONFIG_SRCDIR([src/zidx.h])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_MACRO_DIRS([m4])
AC_REQUIRE_AUX_FILE([tap-driver.sh])

AM_INIT_AUTOMAKE([foreign -Wall -Werror subdir-objects])

# Checks for programs.
AM_PROG_AR
AC_PROG_CC
AC_PROG_CXX
AC_PROG_INSTALL
AC_PROG_LIBTOOL
AC_PROG_AWK

# pkg-config
PKG_PROG_PKG_CONFIG
PKG_INSTALLDIR

# Initialize libtool to build .la files
LT_INIT

# Checks for libraries.
PKG_CHECK_MODULES([STREAMLIKE], [streamlike >= 1.0.0-dev])
PKG_CHECK_MODULES([ZLIB], [zlib >= 1.2.8])
PKG_CHECK_MODULES([CHECK], [check >= 0.9.6])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_OFF_T
AC_TYPE_SIZE_T

# Checks for libraries and arguments
AC_ARG_ENABLE([debug], AC_HELP_STRING([--enable-debug], [enable debug flag]))
if test "x$enable_debug" != "xyes"; then enable_debug="no"; fi
AM_CONDITIONAL([ENABLE_DEBUG], [test x$enable_debug = xyes])

AC_ARG_ENABLE([cpp_interface], AC_HELP_STRING([--disable-cpp-interface],
              [disable c++ interface]))
if test "x$enable_cpp_interface" != "xno"; then enable_cpp_interface="yes"; fi
AM_CONDITIONAL([ENABLE_CPP_INTERFACE], [test x$enable_cpp_interface = xyes])

AM_COND_IF([ENABLE_DEBUG], [
    AC_SUBST([ZIDX_CPPFLAGS], ["-DZX_DEBUG"])
], [
    AC_SUBST([ZIDX_CPPFLAGS], [""])
])

AC_CONFIG_FILES([Makefile
                 src/Makefile
                 tests/Makefile])
AC_OUTPUT

AC_MSG_NOTICE([

C++ Interface.....$enable_cpp_interface
Debug Mode........$enable_debug
])

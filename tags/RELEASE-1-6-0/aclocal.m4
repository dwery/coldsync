# aclocal.m4
#
# Local macros for 'configure' scripts.
#
#	Copyright (C) 1999, Andrew Arensburger
#
# $Id: aclocal.m4,v 1.1 1999-08-25 05:15:41 arensb Exp $

# XXX - This won't compile on systems that use 'struct direct'. Then
# again, is that bad?

AC_DEFUN(CS_DIRENT_TYPE,
[AC_REQUIRE([AC_HEADER_DIRENT])dnl
AC_CACHE_CHECK([for d_type in struct dirent], cs_cv_dirent_type,
[AC_TRY_COMPILE([
#include <sys/types.h>
#include <$ac_header_dirent>
],[struct dirent d; d.d_type],
cs_cv_dirent_type=yes, cs_cv_dirent_type=no)])
if test "$cs_cv_dirent_type" = yes; then
  AC_DEFINE(HAVE_DIRENT_TYPE)
fi
])

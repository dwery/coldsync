# aclocal.m4
#
# Local macros for 'configure' scripts.
#
#	Copyright (C) 1999, Andrew Arensburger
#
# $Id: aclocal.m4,v 1.3 2000-12-23 11:30:24 arensb Exp $

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

# CS_CHECK_GNU(name,path)
# E.g., CS_CHECK_GNU([c++],[/usr/bin/c++])
#
# See if 'path' is a GNU program. If you invoke it as "<path> --version",
# it must exit with a status of 0, and its output must contain the string
# "GNU".
# 'name' is the program's name for purposes of constructing ac_cv_*
# variables, and is also printed in the "checking" message.
#
# If the program is GNU, sets ac_cv_have_gnu_<name> to "yes". Otherwise,
# sets it to "no".
AC_DEFUN(CS_CHECK_GNU,dnl
[AC_CACHE_CHECK([for GNU $1],ac_cv_have_gnu_$1,dnl
[if "$2" --version 2>&1 < /dev/null | grep "GNU" > /dev/null; then
	ac_cv_have_gnu_$1=yes
else
	ac_cv_have_gnu_$1=no
fi])])

# CS_CHECK_GNU_PROGS(variable,progs-to-check-for,value-if-not-found)
# A convenience wrapper around AC_CHECK_PROGS and CS_CHECK_GNU: checks to
# see if the program exists. If yes, also checks whether it's a GNU
# program.

# XXX - This is somewhat misleading: the name contains "PROGS", and the
# syntax is that of AC_CHECK_PROGS (except that the third argument is
# mandatory), but this macro assumes that the second argument is a single
# word. Hence, you can't use this with multiple programs (e.g.,
#	CS_CHECK_GNU_PROGS(TAR, tar gtar, true)
AC_DEFUN(CS_CHECK_GNU_PROGS,dnl
[AC_CHECK_PROGS($1,$2,$3)
 if test x"$[$1]" != x"$3"; then
	CS_CHECK_GNU($2,$[$1])
 fi])

dnl CS_CHECK_TYPE(TYPE, DEFAULT, HEADERS)
dnl Check for TYPE in non-standard places: includes HEADERS. If TYPE cannot
dnl be found, define it to be DEFAULT.
AC_DEFUN(CS_CHECK_TYPE,
[AC_REQUIRE([AC_HEADER_STDC])dnl
AC_MSG_CHECKING(for $1)
AC_CACHE_VAL(ac_cv_type_$1,
[AC_EGREP_CPP(dnl
changequote(<<,>>)dnl
<<(^|[^a-zA-Z_0-9])$1[^a-zA-Z_0-9]>>dnl
changequote([,]), [#include <sys/types.h>
#if STDC_HEADERS
#include <stdlib.h>
#include <stddef.h>
#endif
$3], ac_cv_type_$1=yes, ac_cv_type_$1=no)])dnl
AC_MSG_RESULT($ac_cv_type_$1)
if test $ac_cv_type_$1 = no; then
  AC_DEFINE($1, $2)
fi
])


dnl # This is for Emacs's benefit:
dnl # Local Variables:      ***
dnl # mode: auto-fill       ***
dnl # fill-column:  75      ***
dnl # End:                  ***

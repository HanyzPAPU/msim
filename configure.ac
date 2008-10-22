AC_INIT

AC_PROG_CC
AC_PROG_INSTALL

AC_PATH_PROG(STRIP, strip, no, $PATH:/sbin:/usr/sbin:/usr/local/sbin)
if [[ "$STRIP" = "no" ]] ; then
	AC_MSG_ERROR("Utility strip not found.")
fi

AC_CHECK_LIB(readline, readline,, [AC_MSG_FAILURE(Library readline not found.)])
AC_CHECK_LIB(wsock32, main)

AC_HEADER_STDC
AC_CHECK_HEADERS([ \
	ctype.h \
	errno.h \
	fcntl.h \
	getopt.h \
	readline/history.h \
	readline/readline.h \
	signal.h \
	stdarg.h \
	stdbool.h \
	stdint.h \
	stdio.h \
	stdlib.h \
	string.h \
	sys/stat.h \
	sys/time.h \
	sys/types.h \
	time.h \
	unistd.h \
],, [AC_MSG_FAILURE(Standard header not found.)])

AC_C_BIGENDIAN
AC_C_CONST
AC_TYPE_SIZE_T

AC_CHECK_FUNCS([getopt_long],, [AC_MSG_FAILURE(Function getopt_long not defined.)])

AC_SUBST(BUILDLIBS)
AC_SUBST(AFTERBUILD)
AC_SUBST(INSTALL_PREFIX)

AC_PREFIX_DEFAULT(/usr/local)
if [[ $prefix = "NONE" ]] ; then
	AC_DEFINE(INSTALL_PREFIX, "/usr/local")
else
	AC_DEFINE(INSTALL_PREFIX, "$prefix")
fi

AC_CONFIG_FILES([Makefile src/Makefile])

AC_OUTPUT
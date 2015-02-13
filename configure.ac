#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.63])
AC_INIT(groonga-jmat, 1.3, [kazuma.takaoka@justsystems.com])
AM_INIT_AUTOMAKE([foreign])
AC_CONFIG_SRCDIR([jema.cpp])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])

# Checks for programs.
AC_PROG_CXX
AC_PROG_CC
AC_PROG_LIBTOOL

# Checks for libraries.
PKG_CHECK_MODULES([GROONGA], [groonga >= 2.0.9],
    [pluginsdir=`$PKG_CONFIG --variable=pluginsdir groonga 2>&1`
    AC_SUBST(pluginsdir)
    ])
PKG_CHECK_MODULES([JMAT], [jmat >= 1.0.5])

# Checks for header files.

# Checks for typedefs, structures, and compiler characteristics.
AC_HEADER_STDBOOL
AC_TYPE_SIZE_T

# Checks for library functions.

query_expanders_pluginsdir="\${pluginsdir}/query_expanders"
AC_SUBST(query_expanders_pluginsdir)

tokenizer_pluginsdir="\${pluginsdir}/tokenizers"
AC_SUBST(tokenizer_pluginsdir)

AC_CONFIG_FILES([Makefile])
AC_OUTPUT

#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.59])
AC_INIT([lauditd], [0.1], [sthiell@stanford.edu])
LAUDITD_RELEASE="1"

AC_CONFIG_AUX_DIR([.autotools])
AC_CONFIG_MACRO_DIR([.autotools/m4])
AC_CONFIG_SRCDIR([src/lauditd/lauditd.c])
AC_CONFIG_HEADERS([src/include/lauditd_config.h])

AM_INIT_AUTOMAKE([-Wall -Werror foreign])
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES])


AC_PREFIX_DEFAULT([/usr])

# only generate dynamic libs (used in LD_PRELOAD)
AC_ENABLE_STATIC(no)

# Checks for programs.
AC_GNU_SOURCE
AC_PROG_CC
AM_PROG_AR
AM_PROG_CC_C_O
AC_PROG_LIBTOOL

lauditd_lib_version=`echo $VERSION | sed -e "s/\./:/g"`
AC_SUBST(lauditd_lib_version)

# Checks for libraries.

# Checks for header files.
AC_CHECK_HEADERS([fcntl.h stdlib.h string.h sys/ioctl.h syslog.h unistd.h])
#AC_CHECK_HEADER([lustreapi.h], , AC_MSG_ERROR([liblustreapi is required]))

AC_CHECK_TYPE(struct changelog_ext_jobid, [have_changelog_ext_jobid="yes"],
              [have_changelog_ext_jobid="no"],
              [
              #define LPX64 "%#llx"
              #include <sys/types.h>
              #include <asm/types.h>
              #include <lustre/lustre_user.h>
])
test "x$have_changelog_ext_jobid" = "xyes" && AC_DEFINE(HAVE_CHANGELOG_EXT_JOBID, 1, [struct changelog_ext_jobid is defined])

# Checks for typedefs, structures, and compiler characteristics.
AC_C_INLINE

# Checks for library functions.
AC_FUNC_LSTAT_FOLLOWS_SLASHED_SYMLINK

AC_ARG_ENABLE( [debug], AS_HELP_STRING([--enable-debug],
               [Enable debug traces to stderr for client]),
               [do_debug="$enableval"],[do_debug="no"] )
test "x$do_debug" = "xyes" && AC_DEFINE(_DEBUG, 1, [client debug traces are enabled])

AC_DEFINE_UNQUOTED(RELEASE, "$LAUDITD_RELEASE", [release info] )
AC_SUBST(LAUDITD_RELEASE)


AC_DEFINE_UNQUOTED([LAUDITD_LIBDIR], "/usr/\$LIB", [lauditd client lib installation dir])

# for exporting to spec file
AC_SUBST(ac_configure_args)

AC_CONFIG_FILES([Makefile
                 src/Makefile
                 src/lauditd/Makefile
                 lauditd.spec])
AC_OUTPUT

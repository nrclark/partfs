
#------------------------------------------------------------------------------#

m4_include([m4/semantic_version.m4])
SEMANTIC_VERSION_SET_FILE([VERSION])
AC_INIT([partfs], SEMANTIC_VERSION, [nicholas.clark@gmail.com])
AC_SUBST(MANPAGE_DATE, "February 2021")

#------------------------ Configure Source Locations  -------------------------#

AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_AUX_DIR([build-aux])
AC_REQUIRE_AUX_FILE([tap-driver.sh])
AC_CONFIG_SRCDIR([src/partfs.c])
AC_CONFIG_HEADERS([config.h])

#------------------------- Init Automake / Autoconf  --------------------------#

AC_PREREQ([2.69])
AM_INIT_AUTOMAKE([foreign subdir-objects])
AC_USE_SYSTEM_EXTENSIONS

#------------------------ Confirm Rand() Requirements  ------------------------#

# Checks for programs.
AC_PROG_CC
AC_PROG_INSTALL

AM_PROG_AR
AM_PROG_LIBTOOL
LT_INIT

#------------------------------------------------------------------------------#

AX_REQUIRE_HEADERS([errno.h inttypes.h stdio.h limits.h stddef.h stdlib.h \
                    string.h unistd.h libgen.h], partfs)

AC_FUNC_LSTAT_FOLLOWS_SLASHED_SYMLINK
AC_FUNC_STRNLEN

AC_TYPE_UID_T
AC_C_INLINE
AC_TYPE_MODE_T
AC_TYPE_OFF_T
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T

AX_REQUIRE_FUNCTIONS([memset strerror basename fprintf fstat fsync futimens \
                      getgid getuid lseek lstat memcpy memset stat strcmp \
                      strerror], partfs)

#------------------------------------------------------------------------------#

AC_CHECK_SIZEOF([off_t])
AS_IF([test "x$ac_cv_sizeof_off_t" = x8],
[
  AS_VAR_APPEND([CPPFLAGS],[" -D_FILE_OFFSET_BITS=64"])
])

AC_CHECK_HEADER([fuse.h], [], [AC_CHECK_HEADER([fuse/fuse.h],[],
  [AC_MSG_ERROR([Missing header fuse.h required for partfs.])])])

AC_CHECK_LIB(fuse, fuse_main,,
    [AC_CHECK_LIB(osxfuse, fuse_main,,
      [AC_MSG_ERROR([Missing FUSE library required for partfs.])]
    )]
)

AX_REQUIRE_FUNCTIONS([fuse_get_context fuse_opt_add_arg fuse_opt_parse])

#--------------------- Create Custom Configuration Options --------------------#

AX_CREATE_ENABLE_HELP_SECTION([Features to enable])
AX_MAKE_ENABLE_OPT([sanitizers], [no], [Build with GCC sanitizers enabled])
AX_MAKE_ENABLE_OPT([lint], [no], [Build with every warning GCC can emit])
AX_MAKE_ENABLE_OPT([warnings], [no], [Build with -Wall -Wextra -pedantic])
AX_MAKE_ENABLE_OPT([werror], [no], [Build with -Werror])
AX_MAKE_ENABLE_OPT([partitions], [yes],
                   [Enable partition-select. Requires libfdisk.])

#------------------- Enable partition-selection with libfdisk -----------------#

AM_CONDITIONAL([ENABLE_PARTITIONS],[test x$enable_partitions = xyes])
AC_SUBST([ENABLE_PARTITIONS], $enable_partitions)

AS_IF([test "x$enable_partitions" = xyes],
  [
    AC_DEFINE([ENABLE_PARTITIONS], [1], [Enable support for mount-by-partition])

    AC_CHECK_HEADER([libfdisk.h], [], [AC_CHECK_HEADER([libfdisk/libfdisk.h],[],
      [AC_MSG_ERROR([Missing header libfdisk.h required for partfs.])])])

    AC_CHECK_LIB(fdisk, fdisk_get_partitions, [],
      [AC_MSG_ERROR([Libfdisk is required unless partition-select support is disabled.])])
  ],
  []
)

#---------------------- Configure For Optional Sanitizers  --------------------#

AS_IF([test "x$enable_lint" = xno], [], [
  AX_ENABLE_CFLAGS([-std=c99])
  AX_ENABLE_EVERY_WARNING([-Wtraditional -Wtraditional-conversion \
                           -Wabi -Wpadded -Wformat-nonliteral -Wc++-compat])
])

AS_IF([test "x$enable_warnings" = xno], [], [
  AX_ENABLE_CFLAGS([
    -Wall
    -Wextra
    -pedantic
  ])
])

AS_IF([test "x$enable_sanitizers" = xno], [], [
  AX_ENABLE_CFLAGS([
    -O0
    -g
    -fstack-protector-strong
    -fstack-protector-all
    -fsanitize=shift
    -fsanitize=undefined
    -fsanitize=address
    -fsanitize=alignment
    -fsanitize=bool
    -fsanitize=bounds
    -fsanitize=bounds-strict
    -fsanitize=enum
    -fsanitize=float-cast-overflow
    -fsanitize=float-divide-by-zero
    -fsanitize=integer-divide-by-zero
    -fsanitize=null
    -fsanitize=object-size
    -fno-sanitize-recover=all
    -fsanitize=return
    -fsanitize=returns-nonnull-attribute
    -fsanitize=signed-integer-overflow
  ])
])

AS_IF([test "x$enable_werror" = xno], [], [
  AS_VAR_APPEND([CFLAGS],[" -Werror"])
])

#------------------------------ Generate Outputs ------------------------------#

AC_CONFIG_FILES([Makefile src/Makefile man/partfs.1], [],
                [MANPAGE_DATE="$MANPAGE_DATE"])
AC_OUTPUT


# Verifies that a C compiler flag is supported. Throws an AC_MSG_ERROR
# if the flag doesn't work.

AC_DEFUN([AX_CHECK_CFLAGS],
  [
  for flag_break in "$1"; do
    for flag in $flag_break; do
      ORIG_CFLAGS="$CFLAGS"
      AC_MSG_CHECKING([whether compiler supports $flag])
      CFLAGS="$ORIG_CFLAGS $flag"
      AC_TRY_COMPILE(,
         [void f() {};],
         [AC_MSG_RESULT(yes)],
         [AC_MSG_RESULT(no)
          AC_MSG_ERROR([flag '$flag' not supported])]
      )
      CFLAGS="$ORIG_CFLAGS"
      done
    done
  ]
)

# Verifies that a C compiler flag is supported and adds it to CFLAGS.
# Throws an AC_MSG_ERROR if the flag doesn't work.

AC_DEFUN([AX_ENABLE_CFLAGS],
  [
  for flag_break in "$1"; do
    for flag in $flag_break; do
      AX_CHECK_CFLAGS([$flag])
      AS_VAR_APPEND([CFLAGS],[" $flag"])
    done
  done
  ]
)

# $1 is a set of warnings to blacklist. Every other warning the compiler
# knows how to emit will be enabled.
AC_DEFUN([AX_ENABLE_EVERY_WARNING],
  [
  _FLAG_SET="`BLACKLIST="$1" ./guess_flags.sh`"

  AS_VAR_APPEND([CFLAGS],[" -Wall -Wextra -pedantic"])
  for flag in $_FLAG_SET; do
      AS_VAR_APPEND([CFLAGS],[" $flag"])
  done
  AS_VAR_APPEND([CFLAGS],[" -Wno-system-headers"])
  ]
)

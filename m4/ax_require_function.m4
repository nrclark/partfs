AC_DEFUN([AX_REQUIRE_FUNCTION], [AC_CHECK_FUNC([$1],,
         [AC_MSG_ERROR([Missing function "$1" required for $2.])])])

AC_DEFUN([AX_REQUIRE_HEADER], [AC_CHECK_HEADER([$1],,
         [AC_MSG_ERROR([Missing header "$1" required for $2.])])])

# Forces a set of functions to be present. Throws an AC_MSG_ERROR if any
# of them are missing.

AC_DEFUN([AX_REQUIRE_FUNCTIONS],
  [
  for function_break in "$1"; do
    for function in $function_break; do
      AX_REQUIRE_FUNCTION([$function],[$2])
    done
  done
  ]
)

# Forces a set of headers to be present. Throws an AC_MSG_ERROR if any
# of them are missing.

AC_DEFUN([AX_REQUIRE_HEADERS],
  [
  for header_break in "$1"; do
    for header in $header_break; do
      AX_REQUIRE_HEADER([$header],[$2])
    done
  done
  ]
)

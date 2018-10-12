AC_DEFUN([AX_MAKE_ENABLE_OPT],
    [AC_ARG_ENABLE(
        [$1],
        [AS_HELP_STRING([--enable-$1=$2],[$3 [default=$2]])],
        [enable_$1=$enableval],
        [enable_$1=$2]
    )]
)


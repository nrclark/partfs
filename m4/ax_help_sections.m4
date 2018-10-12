
# Creates a visually-delineated block in ./configure's --help 'Optional
# Packages' section. Useful for library-specific options. The input parameter
# is the name of the new section.

AC_DEFUN([AX_CREATE_WITH_HELP_SECTION],[AC_ARG_WITH([__dummy_with__],
[
$1:], [],[])])

# Creates a visually-delineated block in ./configure's --help 'Optional
# Features' section. Useful for library-specific options. The input parameter
# is the name of the new section.

AC_DEFUN([AX_CREATE_ENABLE_HELP_SECTION],[AC_ARG_ENABLE([__dummy_enable__],
[
$1:], [],[])])

.SUFFIXES:

CC := gcc
FORCED_CFLAGS := -D_FILE_OFFSET_BITS=64

default:

MAKE_DIR := $(abspath $(dir $(firstword $(MAKEFILE_LIST))))

#------------------------------------------------------------------------------#

lint_flags.mk:
	$(CC) -Wall -Wextra -pedantic -Q --help=warning | \
	grep -P "(disabled|[=] )" | grep -Po "[-]W[^ \t=]+" | \
	sort | uniq > $@.temp.init
	echo "int main(void) { return 0;}" > $@.temp.c
	$(CC) $$(cat $@.temp.init) $@.temp.c -o /dev/null 2>&1 | grep "error: " | \
	grep -oP "[-]W[a-zA-Z0-9_-]+" | sort | uniq > $@.temp.blacklist
	cat $@.temp.init | grep -vFf $@.temp.blacklist > $@.temp.works
	$(CC) $$(cat $@.temp.works) $@.temp.c -o /dev/null 2>&1 | \
	    grep -P "is valid for [^ ]+ but not for C" | \
	    grep -oP "[-]W[a-zA-Z0-9_-]+" > $@.temp.blacklist
	cat $@.temp.works | grep -vFf $@.temp.blacklist > $@.temp.ok
	echo 'LINT_CFLAGS := \' > $@
	cat $@.temp.ok | sed 's/$$/ \\/g' >> $@
	echo "" >> $@
	rm -f $@.*

-include lint_flags.mk

LINT_BLACKLIST := \
    -Wtraditional -Wformat-nonliteral -Wtraditional-conversion -Wpadded \
    -Wunused-macros -Wabi

LINT_CFLAGS := $(strip \
    -Wall -Wextra -pedantic -std=c99 \
    $(filter-out $(LINT_BLACKLIST),$(LINT_CFLAGS)) \
    -Wno-system-headers \
)

LINT_CFLAGS += $(FORCED_CFLAGS)

lint-%.h: %.h
	@echo Checking $*.h against lots of compiler warnings...
	@$(CC) -c $(LINT_CFLAGS) -Wno-unused-macros $< -o /dev/null

lint-%.c: %.c
	@echo Checking $*.c against lots of compiler warnings...
	@$(CC) -c $(LINT_CFLAGS) $< -o /dev/null

LINT_TARGETS := $(foreach x,$(wildcard *.c),lint-$x)
LINT_TARGETS += $(foreach x,$(wildcard *.h),lint-$x)
LINT_TARGETS := $(sort $(LINT_TARGETS))
$(foreach x,$(LINT_TARGETS),$(eval $x:))

lint: $(foreach x,$(LINT_TARGETS),$x)

clean::
	rm -f *.plist

#------------------------------------------------------------------------------#

format-%: % $(MAKE_DIR)/uncrustify.cfg
	@echo Formatting $*...
	@cp -a $* $*.temp.c
	@uncrustify -c $(MAKE_DIR)/uncrustify.cfg --no-backup $*.temp.c -lc -q
	@sed -i 's/^    "/"/g' $*.temp.c
	@if ! diff -q $*.temp.c $* 1>/dev/null; then cp $*.temp.c $*; fi
	@rm $*.temp.c

$(foreach x,$(wildcard *.c),$(eval format-$x:))
$(foreach x,$(wildcard *.h),$(eval format-$x:))
format: $(foreach x,$(wildcard *.c),format-$x)
format: $(foreach x,$(wildcard *.h),format-$x)

#------------------------------------------------------------------------------#

COMMA := ,
EMPTY :=
SPACE := $(EMPTY) $(EMPTY)
CLANG_TIDY_BLACKLIST := $(strip \
    -llvm-header-guard -android-cloexec-open \
    -android-cloexec-accept -hicpp-signed-bitwise -hicpp-no-assembler \
)

TIDY_CFLAGS := $(FORCED_CFLAGS)

TIDY_BLACKLIST_STRING := $(strip $(if $(CLANG_TIDY_BLACKLIST),\
    $(COMMA)$(subst $(SPACE),$(COMMA),$(CLANG_TIDY_BLACKLIST)),))

tidy-%: %
	@echo Analyzing $* with clang-tidy/clang-check...
	@clang-tidy \
	    "-checks=*$(TIDY_BLACKLIST_STRING)" \
	    "-header-filter=.*" -extra-arg="$(TIDY_CFLAGS)" $* -- 2>/dev/null | \
	    (grep -iP "(warning|error)[:]" -A2 --color || true)
	@clang-check -analyze $* -extra-arg="$(TIDY_CFLAGS)" --
	@rm -f $(basename $*).plist

$(foreach x,$(wildcard *.c),$(eval tidy-$x:))
$(foreach x,$(wildcard *.h),$(eval tidy-$x:))
tidy: $(foreach x,$(wildcard *.c),tidy-$x)
tidy: $(foreach x,$(wildcard *.h),tidy-$x)

#------------------------------------------------------------------------------#

sanitize ?= 1

SAN_CFLAGS := \
    -O0 -g \
    -fstack-protector-strong \
    -fstack-protector-all \
    -fsanitize=shift \
    -fsanitize=undefined \
    -fsanitize=address \
    -fsanitize=alignment \
    -fsanitize=bool \
    -fsanitize=bounds \
    -fsanitize=bounds-strict \
    -fsanitize=enum \
    -fsanitize=float-cast-overflow \
    -fsanitize=float-divide-by-zero \
    -fsanitize=integer-divide-by-zero \
    -fsanitize=null \
    -fsanitize=object-size \
    -fsanitize=leak \
    -fno-sanitize-recover=all \
    -fsanitize=return \
    -fsanitize=vla-bound \
    -fsanitize=unreachable \
    -fsanitize=returns-nonnull-attribute \
    -fsanitize=signed-integer-overflow \
    -fstack-check

#------------------------------------------------------------------------------#

cppcheck-%:
	@echo -n "(cppcheck) "
	@(cppcheck $* --std=c99 --force \
	--enable=warning,style,performance,portability \
	-I `pwd` -I /usr/include -I /usr/include/linux \
	-I /usr/lib/gcc/x86_64-redhat-linux/7/include \
	--std=c99) 2>&1 | (grep -vP "^[(]information" 1>&2 || true)

$(foreach x,$(wildcard *.c),$(eval cppcheck-$x:))
$(foreach x,$(wildcard *.h),$(eval cppcheck-$x:))
cppcheck: $(foreach x,$(wildcard *.c),cppcheck-$x)
cppcheck: $(foreach x,$(wildcard *.h),cppcheck-$x)

include-%:
	@(include-what-you-use \
	    -I/usr/lib/gcc/x86_64-redhat-linux/7/include $* || true) 2>&1 | \
	    grep -P "(should remove these lines|has correct [#]includes)" || true
	@(include-what-you-use \
	    -I/usr/lib/gcc/x86_64-redhat-linux/7/include $* || true) 2>&1 | \
	    grep -P "^[-] " || true

fullinclude-%:
	-include-what-you-use -I/usr/lib/gcc/x86_64-redhat-linux/7/include $*

include: $(foreach x,$(wildcard *.c),include-$x)
include: $(foreach x,$(wildcard *.h),include-$x)
$(foreach x,$(wildcard *.c),$(eval include-$x:))
$(foreach x,$(wildcard *.h),$(eval include-$x:))

#------------------------------------------------------------------------------#

combo-%: format-% cppcheck-% lint-% tidy-%
	@echo 1>/dev/null

combo: $(foreach x,$(wildcard *.c),include-$x)
combo: $(foreach x,$(wildcard *.h),include-$x)
$(foreach x,$(wildcard *.c),$(eval combo-$x:))
$(foreach x,$(wildcard *.h),$(eval combo-$x:))

#------------------------------------------------------------------------------#

CFLAGS := -Wall -Wextra -pedantic
ifneq ($(sanitize),)
CFLAGS += $(SAN_CFLAGS)
else
CFLAGS += -O2
endif

CFLAGS := $(strip $(CFLAGS) $(FORCED_CFLAGS))

%.o: %.c
	$(CC) -c $(CFLAGS) $< -o $@

clean::
	rm -f *.o

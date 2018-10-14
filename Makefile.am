ACLOCAL_AMFLAGS = -I m4 --install
EXTRA_DIST = VERSION README.md autogen.sh guess_flags.sh

bin_PROGRAMS = partfs
partfs_SOURCES = partfs.c

TEST_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) $(top_srcdir)/build-aux/tap-driver.sh
TEST_LOG_DRIVER_FLAGS = --comments
TESTS_ENVIRONMENT = PATH=$(abs_srcdir)/test:$(abs_builddir):$(PATH)
TESTS = \
    test/read.test \
    test/mount_options.test

EXTRA_DIST += $(TESTS)

ACLOCAL_AMFLAGS = -I m4 --install
EXTRA_DIST = VERSION README.md autogen.sh guess_flags.sh

SUBDIRS = po
EXTRA_DIST += po/Makevars.in
DISTCLEANFILES = po/Makevars

bin_PROGRAMS = partfs
partfs_SOURCES = partfs.c
dist_man_MANS = man/partfs.1

TEST_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) $(top_srcdir)/build-aux/tap-driver.sh
TEST_LOG_DRIVER_FLAGS = --comments
TESTS_ENVIRONMENT = PATH=$(abs_srcdir)/test:$(abs_builddir):$(PATH)
TESTS = \
    test/mount_options.test \
    test/read.test \
    test/write.test

EXTRA_DIST += test/reader.py test/taplib.sh test/writer.py
EXTRA_DIST += $(TESTS)

TESTS = check_libzidx
LOG_DRIVER = env CK_TAP_LOG_FILE_NAME='-' AM_TAP_AWK='$(AWK)' \
             '$(SHELL)' '$(top_srcdir)/build-aux/tap-driver.sh'

AM_CPPFLAGS = -I$(top_builddir)/src @ZIDX_CPPFLAGS@
AM_CFLAGS = -std=gnu11 @CHECK_CFLAGS@ @ZLIB_CFLAGS@ @STREAMLIKE_CFLAGS@
LDADD = $(top_builddir)/src/libzidx.la -l:libpcg_random.a @CHECK_LIBS@ @ZLIB_LIBS@ @STREAMLIKE_LIBS@

check_PROGRAMS = check_libzidx
check_libzidx_SOURCES = check_libzidx.c utils.c utils.h $(top_builddir)/src/zidx.h
check_libzidx_CFLAGS = $(CFLAGS)
check_libzidx_LDADD = $(LDADD)

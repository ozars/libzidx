TESTS = check_libzidx

check_PROGRAMS = check_libzidx
check_libzidx_SOURCES = check_libzidx.c utils.c utils.h $(top_builddir)/src/zidx.h
check_libzidx_CFLAGS = @CHECK_CFLAGS@ -I$(top_builddir)/src
check_libzidx_LDADD = $(top_builddir)/src/libzidx.a -lz -l:libpcg_random.a @CHECK_LIBS@

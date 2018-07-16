TESTS = check_libzidx

check_PROGRAMS = check_libzidx
check_libzidx_SOURCES = check_libzidx.c utils.c utils.h $(top_builddir)/src/zidx.h
check_libzidx_CFLAGS = @CHECK_CFLAGS@ -I../src/
check_libzidx_LDADD = $(top_builddir)/src/libzidx.a @CHECK_LIBS@
check_libzidx_LDFLAGS = -lz -lpcg_random

TESTS = check_stream_api

check_PROGRAMS = check_stream_api
check_stream_api_SOURCES = check_stream_api.c $(top_builddir)/src/zidx.h
check_stream_api_CFLAGS = @CHECK_CFLAGS@
check_stream_api_LDADD = $(top_builddir)/src/libzidx.a @CHECK_LIBS@

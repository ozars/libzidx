if ENABLE_CPP_INTERFACE
CPP_INTERFACE_CPP = zidx_streamlikexx.cpp
CPP_INTERFACE_HPP = zidx_streamlike.hpp
else
CPP_INTERFACE_CPP =
CPP_INTERFACE_HPP =
endif
lib_LTLIBRARIES = libzidx.la
libzidx_la_SOURCES = zidx.c zidx.h zidx_streamlike.c zidx_streamlike.h $(CPP_INTERFACE_CPP) $(CPP_INTERFACE_HPP)
libzidx_la_CFLAGS = -std=gnu11 @STREAMLIKE_CFLAGS@ @ZLIB_CFLAGS@
libzidx_la_CXXFLAGS = @STREAMLIKE_CFLAGS@ @ZLIB_CFLAGS@
libzidx_la_CPPFLAGS = @ZIDX_CPPFLAGS@
libzidx_la_LIBADD = @STREAMLIKE_LIBS@ @ZLIB_LIBS@
include_HEADERS = zidx.h zidx_streamlike.h $(CPP_INTERFACE_HPP)

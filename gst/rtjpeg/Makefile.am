
plugin_LTLIBRARIES = libgstrtjpeg.la

libgstrtjpeg_la_SOURCES = gstrtjpeg.c gstrtjpegenc.c gstrtjpegdec.c RTjpeg.c
libgstrtjpeg_la_CFLAGS = $(GST_CFLAGS)
libgstrtjpeg_la_LIBADD =
libgstrtjpeg_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
if !GST_PLUGIN_BUILD_STATIC
libgstrtjpeg_la_LIBTOOLFLAGS = --tag=disable-static
endif

noinst_HEADERS = gstrtjpegenc.h gstrtjpegdec.h RTjpeg.h


plugin_LTLIBRARIES = libgstchart.la

libgstchart_la_SOURCES = gstchart.c
libgstchart_la_CFLAGS = $(GST_CFLAGS)
libgstchart_la_LIBADD =
libgstchart_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
if !GST_PLUGIN_BUILD_STATIC
libgstchart_la_LIBTOOLFLAGS = --tag=disable-static
endif



plugin_LTLIBRARIES = libgstmpeg1systemencode.la

libgstmpeg1systemencode_la_SOURCES = gstmpeg1systemencode.c \
			 buffer.c \
			 systems.c  
libgstmpeg1systemencode_la_CFLAGS = $(GST_CFLAGS)
libgstmpeg1systemencode_la_LIBADD =
libgstmpeg1systemencode_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
if !GST_PLUGIN_BUILD_STATIC
libgstmpeg1systemencode_la_LIBTOOLFLAGS = --tag=disable-static
endif

noinst_HEADERS = gstmpeg1systemencode.h \
		 main.h \
		 buffer.h

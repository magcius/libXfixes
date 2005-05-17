lib_LTLIBRARIES = libXfixes.la

libXfixes_la_SOURCES = 	\
	Cursor.c	\
	Region.c	\
	SaveSet.c	\
	Selection.c	\
	Xfixes.c	\
	Xfixesint.h

libXfixes_la_LIBADD = @X_LIBS@

INCLUDES = -I$(top_srcdir)/include/X11/extensions

#
# Library version info.  Check the libtool docs for
# instructions on when and how to change this value
#
# bump when the ABI changes
XFIXES_CURRENT=2
# bump for non-ABI changes, reset to zero when CURRENT changes
XFIXES_REVISION=0
# bump when the ABI changes in backward-compatible fashion
XFIXES_AGE=2
libXfixes_la_LDFLAGS = -version-info ${XFIXES_CURRENT}:${XFIXES_REVISION}:${XFIXES_AGE} -no-undefined

libXfixesincludedir = $(includedir)/X11/extensions
libXfixesinclude_HEADERS = $(top_srcdir)/include/X11/extensions/Xfixes.h 


LIBS = $(GROONGA_LIBS) $(JMAT_LIBS)
AM_LDFLAGS = -avoid-version -module -no-defined
ACLOCAL_AMFLAGS = -I m4

query_expanders_plugins_LTLIBRARIES = jetmctrl.la
tokenizer_plugins_LTLIBRARIES = jema.la

jema_la_SOURCES	= jema.cpp jmat_mark.h
jema_la_CPPFLAGS = $(GROONGA_CFLAGS) $(JMAT_CFLAGS)
jema_la_LIBADD = $(LIBS)
jema_la_LDFLAGS = $(AM_LDFLAGS) $(GROONGA_LDFLAGS) $(JMAT_LDFLAGS)

jetmctrl_la_SOURCES = jetmctrl.cpp jmat_mark.h
jetmctrl_la_CPPFLAGS = $(GROONGA_CFLAGS) $(JMAT_CFLAGS)
jetmctrl_la_LIBADD = $(LIBS)
jetmctrl_la_LDFLAGS = $(AM_LDFLAGS) $(GROONGA_LDFLAGS) $(JMAT_LDFLAGS)

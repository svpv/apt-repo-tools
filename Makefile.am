AM_CXXFLAGS = -std=c++11

bin_PROGRAMS = genpkglist gensrclist pkglist-query
bin_SCRIPTS = genbasedir

EXTRA_DIST = genbasedir

genpkglist_SOURCES = genpkglist.cc cached_md5.cc cached_md5.h genutil.h zhdr.h slab.h
gensrclist_SOURCES = gensrclist.cc cached_md5.cc cached_md5.h genutil.h zhdr.h
genpkglist_LDADD = $(LZ4_LIBS)
gensrclist_LDADD = $(LZ4_LIBS)
pkglist_query_SOURCES = pkglist-query.cc

AC_INIT([apt-repo-tools],[0.6.0],[at@altlinux.org])
AC_CONFIG_SRCDIR([genpkglist.cc])
AM_INIT_AUTOMAKE([foreign])

AC_PROG_CXX
AC_SYS_LARGEFILE
AC_PROG_LIBTOOL

AC_MSG_CHECKING(for RPM version)
RPM_VERSION_RAW=`LANG=C LC_ALL=C rpm --version | sed 's/^RPM version //'`
RPM_VERSION_MAJOR=`echo $RPM_VERSION_RAW | cut -d. -f1`
RPM_VERSION_MINOR=`echo $RPM_VERSION_RAW | cut -d. -f2`
RPM_VERSION_MICRO=`echo $RPM_VERSION_RAW | cut -d. -f3`
if test -z "$RPM_VERSION_MAJOR" -o -z "$RPM_VERSION_MINOR"; then
	AC_MSG_ERROR([can't parse RPM version])
fi
if test -z "$RPM_VERSION_MICRO"; then
	RPM_VERSION_MICRO=0
fi
AC_DEFINE_UNQUOTED([RPM_VERSION_MAJOR],$RPM_VERSION_MAJOR,[RPM major version])
AC_DEFINE_UNQUOTED([RPM_VERSION_MINOR],$RPM_VERSION_MINOR,[RPM minor version])
AC_DEFINE_UNQUOTED([RPM_VERSION_MICRO],$RPM_VERSION_MICRO,[RPM micro version])
AC_DEFINE_UNQUOTED([RPM_VERSION],
	[((RPM_VERSION_MAJOR<<16)|(RPM_VERSION_MINOR<<8)|RPM_VERSION_MICRO)],
	[Define to the RPM version])
AC_DEFINE_UNQUOTED([RPM_VERSION_RAW],"$RPM_VERSION_RAW",[RPM raw version])
AC_MSG_RESULT($RPM_VERSION_RAW)

AC_CHECK_HEADER([rpm/rpmlib.h],,
	[AC_MSG_ERROR([rpm headers not found])] )
AC_CHECK_LIB([rpm],[main],,
	[AC_MSG_ERROR([rpm library not found])] )
AC_CHECK_LIB([rpmio],[main],,
	[AC_MSG_ERROR([rpmio library not found])] )
AC_CHECK_FUNCS(headerFormat)

AC_LANG_CPLUSPLUS
AC_CHECK_HEADER([apt-pkg/configuration.h],,
	[AC_MSG_ERROR([apt-pkg headers not found])] )
AC_CHECK_LIB([apt-pkg], [main],,
	[AC_MSG_ERROR([apt-pkg library not found])] )

AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([Makefile])
AC_OUTPUT

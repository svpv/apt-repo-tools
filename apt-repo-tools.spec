Name: apt-repo-tools
Version: 0.6.0
Release: alt0

Summary: Utilities to create APT repositories
License: GPL
Group: Development/Other

Source: %name-%version.tar

Provides: apt-utils = 0.5.15lorg4
Obsoletes: apt-utils <= 0.5.15lorg4

BuildRequires: gcc-c++ libapt-devel librpm-devel

%description
This package contains the utility programs that can prepare a repository
of RPMS binary and source packages for future access by APT (by generating
the indices): genbasedir, genpkglist, gensrclist.

%prep
%setup -q

%build
autoreconf -i
%configure
make

%install
make install DESTDIR=%buildroot

%files
/usr/bin/genpkglist
/usr/bin/gensrclist
/usr/bin/genbasedir
/usr/bin/pkglist-query

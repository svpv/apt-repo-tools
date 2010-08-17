Name: apt-repo-tools
Version: 0.6.0.7
Release: alt1

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
mkdir -p %buildroot/var/cache/apt/genpkglist
mkdir -p %buildroot/var/cache/apt/gensrclist

%files
/usr/bin/genpkglist
/usr/bin/gensrclist
/usr/bin/genbasedir
/usr/bin/pkglist-query
%defattr(2770,root,rpm,2770)
%dir /var/cache/apt/genpkglist
%dir /var/cache/apt/gensrclist

%changelog
* Tue Aug 17 2010 Alexey Tourbin <at@altlinux.ru> 0.6.0.7-alt1
- genbasedir: implemented --maybe-unchanged option, for use in girar-builder

* Thu Jun 10 2010 Alexey Tourbin <at@altlinux.ru> 0.6.0.6-alt1
- genbasedir: added support for xz-compressed pkglists
- genbasedir: run bzip2 and xz in background

* Mon Dec 21 2009 Alexey I. Froloff <raorn@altlinux.org> 0.6.0.5-alt1.1
- NMU:
  + rebuilt with apt 0.5.15lorg2-alt31.1

* Sun Oct 25 2009 Alexey Tourbin <at@altlinux.ru> 0.6.0.5-alt1
- genpkglist.cc: implemented "--changelog-since seconds" option
- genbasedir: implemented --changelog-since=DATE option

* Sun Oct 25 2009 Alexey Tourbin <at@altlinux.ru> 0.6.0.4-alt1
- genpkglist.cc: group binary packages by src.rpm

* Sun Sep 27 2009 Alexey Tourbin <at@altlinux.ru> 0.6.0.3-alt1
- genpkglist.cc: fixed first-pass memory leak

* Tue Jul 14 2009 Alexey Tourbin <at@altlinux.ru> 0.6.0.2-alt1
- genbasedir: replaced 'bzip2 -9' with 'bzip2', to make BZIP2=--fast work

* Thu Jul 09 2009 Alexey Tourbin <at@altlinux.ru> 0.6.0.1-alt1
- genpkglist: added /usr/games and /usr/lib/kde4bin directories

* Wed Apr 22 2009 Alexey Tourbin <at@altlinux.ru> 0.6.0-alt1
- this package provides and obsoletes apt-utils
- genpkglist: reimplemented support for file-level dependencies
- genpkglist: removed /etc/ from usefulFile patterns
- genpkglist: file dups are now stripped as well
- genpkglist: added --useful-files=FILE option
- genpkglist: added --no-scan option
- genbasedir: pass --no-scan and --useful-files=FILE to genpkglist
- genbasedir: pass --cache-dir=DIR to genpkglist and gensrclist
- pkglist-query: new program, obsoletes countpkglist

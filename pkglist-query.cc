#include <stdio.h>
#include <rpm/rpmlib.h>
#include "config.h"

int main(int argc, char *argv[])
{
    const char *progname = argv[0];
    if (argc < 3) {
	fprintf(stderr, "Usage: %s <format> <pkglist>...\n", progname);
	return 2;
    }
    const char *format = argv[1];
    int rc = 0;
    const char *pkglist;
    int ix = 2;
    while ((pkglist = argv[ix++]) != NULL) {
	FD_t Fd = Fopen(pkglist, "r.ufdio");
	if (Ferror(Fd)) {
	    fprintf(stderr, "%s: %s: %s\n", progname, pkglist, Fstrerror(Fd));
	    rc = 1;
	    continue;
	}
	Header h;
	while ((h = headerRead(Fd, HEADER_MAGIC_YES)) != NULL) {
	    const char *err = "unknown error";
#ifdef RPM_HAVE_HEADERFORMAT
	    char *str = headerFormat(h, format, &err);
#else
	    char *str = headerSprintf(h, format, rpmTagTable, rpmHeaderFormats, &err);
#endif
	    if (str == NULL) {
		rc = 1;
		fprintf(stderr, "%s: %s: %s\n", progname, pkglist, err);
	    }
	    else {
		fputs(str, stdout);
		free(str);
	    }
	    headerFree(h);
	}
	Fclose(Fd);
    }
    return rc;
}

// ex:set ts=8 sts=4 sw=4 noet:

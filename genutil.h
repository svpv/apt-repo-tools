
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

static inline
bool startswith(const char *str, const char *prefix)
{
   size_t len1 = strlen(str);
   size_t len2 = strlen(prefix);
   if (len1 < len2)
      return false;
   return memcmp(str, prefix, len2) == 0;
}

static inline
bool endswith(const char *str, const char *suffix)
{
   size_t len1 = strlen(str);
   size_t len2 = strlen(suffix);
   if (len1 < len2)
      return false;
   str += (len1 - len2);
   return memcmp(str, suffix, len2) == 0;
}

static
#if defined(__APPLE__) || defined(__FREEBSD__)
int selectRPMs(struct dirent *ent)
#else
int selectRPMs(const struct dirent *ent)
#endif
{
   const char *b = ent->d_name;
   return (*b != '.' && endswith(b, ".rpm"));
}

static int asciisort(const struct dirent **a, const struct dirent **b)
{
    return strcmp((*a)->d_name, (*b)->d_name);
}

static
void simpleProgress(unsigned int current, unsigned int total)
{
   bool erase = (current > 1);
   if (erase) {
      putc('\b', stdout);
      putc('\b', stdout);
   }
   int width = 0;
   unsigned int n = total;
   while (n) {
      n /= 10;
      width++;
      if (erase) {
	 putc('\b', stdout);
	 putc('\b', stdout);
      }
   }
   printf(" %*u/%*u", width, current, width, total);
   fflush(stdout);
}

#if RPM_VERSION >= 0x040100
#include <rpm/rpmts.h>
#endif

static
Header readHeader(const char *path)
{
   FD_t fd = Fopen(path, "r");
   if (fd == NULL)
      return NULL;
   Header h = NULL;
#if RPM_VERSION >= 0x040100
   static rpmts ts = NULL;
   if (ts == NULL) {
      rpmReadConfigFiles(NULL, NULL);
      ts = rpmtsCreate();
      assert(ts);
      rpmtsSetVSFlags(ts, (rpmVSFlags_e)-1);
   }
   int rc = rpmReadPackageFile(ts, fd, path, &h);
   bool ok = (rc == RPMRC_OK || rc == RPMRC_NOTTRUSTED || rc == RPMRC_NOKEY);
#else
   int rc = rpmReadPackageHeader(fd, &h, NULL, NULL, NULL);
   bool ok = (rc == 0);
#endif
   Fclose(fd);
   if (ok)
      return h;
   return NULL;
}

static
bool stmatch(Header h, struct stat const& st)
{
   unsigned st_size = headerGetNumber(h, CRPMTAG_FILESIZE);
   return (unsigned) st.st_size == st_size;
}

static
void copyTag(Header h1, Header h2, raptTag tag)
{
   rpmtd td = rpmtdNew();
   // Copy raw entry, so that internationalized strings
   // will get copied correctly.
   if (headerGet(h1, tag, td, HEADERGET_RAW) == 1) {
      headerPut(h2, td, HEADERPUT_DEFAULT);
      rpmtdFreeData(td);
   }
   rpmtdFree(td);
}

static
void copyTags(Header h1, Header h2, int tagc, raptTag tagv[])
{
   for (int i = 0; i < tagc; i++)
      copyTag(h1, h2, tagv[i]);
}

static
void addAptTags(Header h, const char *d, const char *b, unsigned int st_size)
{
   headerPutString(h, CRPMTAG_DIRECTORY, d);
   headerPutString(h, CRPMTAG_FILENAME, b);
   headerPutUint32(h, CRPMTAG_FILESIZE, &st_size, 1);
}


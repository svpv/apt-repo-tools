
static inline
bool endswith(const char *str, const char *suffix)
{
   size_t len1 = strlen(str);
   size_t len2 = strlen(suffix);
   if (len1 < len2)
      return false;
   str += (len1 - len2);
   if (strcmp(str, suffix))
      return false;
   return true;
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
   int rc = rpmReadPackageFile(ts, fd, dirEntries[entry_cur]->d_name, &h);
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


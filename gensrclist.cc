/*
 * $Id: gensrclist.cc,v 1.8 2003/01/30 17:18:21 niemeyer Exp $
 */
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <alloca.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <rpm/rpmlib.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <assert.h>

#include <map>
#include <list>
#include <vector>
#include <iostream>

#include <apt-pkg/error.h>
#include <apt-pkg/tagfile.h>
#include <apt-pkg/configuration.h>

#include "rapt-compat.h"
#include "crpmtag.h"
#include "cached_md5.h"
#include "genutil.h"

using namespace std;

raptTag tags[] =  {
       RPMTAG_NAME,
       RPMTAG_EPOCH,
       RPMTAG_VERSION,
       RPMTAG_RELEASE,
       RPMTAG_GROUP,
       RPMTAG_ARCH,
       RPMTAG_PACKAGER,
       RPMTAG_SIZE,
       RPMTAG_VENDOR,

       RPMTAG_DESCRIPTION,
       RPMTAG_SUMMARY,
       /*RPMTAG_HEADERI18NTABLE*/ HEADER_I18NTABLE,

       RPMTAG_REQUIREFLAGS, 
       RPMTAG_REQUIRENAME,
       RPMTAG_REQUIREVERSION
};
int numTags = sizeof(tags) / sizeof(tags[0]);

static
void readIndex(FILE *fp, map<string, vector<const char *> > &table)
{
   char line[512];
   while (fgets(line, sizeof(line), fp)) {
      line[strlen(line)-1] = '\0'; // trim newline
      char *val = strchr(line, ' ');
      assert(val);
      *val++ = '\0';
      const char *srpm = line;
      const char *rpm = strdup(val);
      assert(rpm);
      table[srpm].push_back(rpm);
   }
}


void usage()
{
   cerr << "gensrclist " << VERSION << endl;
   cerr << "usage: gensrclist [<options>] <dir> <suffix> <srpm index>" << endl;
   cerr << "options:" << endl;
//   cerr << " --mapi         ???????????????????" << endl;
   cerr << " --flat          use a flat directory structure, where RPMS and SRPMS"<<endl;
   cerr << "                 are in the same directory level"<<endl;
   cerr << " --meta <suffix> create source package file list with given suffix" << endl;
   cerr << " --append        append to the source package file list, don't overwrite" << endl;
   cerr << " --progress      show a progress bar" << endl;
   cerr << " --cachedir=DIR  use a custom directory for package md5sum cache"<<endl;
   cerr << " --prev-stdin    read previous output from stdin and use it as a cache" << endl;
}

class HdlistReader {
   FD_t fd;
   Header h;
   const char *fname;
   bool eof;
public:
   HdlistReader(FD_t fd) : fd(fd), h(NULL), fname(NULL), eof(false)
   { }
   ~HdlistReader()
   {
      headerFree(h);
   }
   Header find(const char *fn, struct stat const& st)
   {
      if (eof)
	 return NULL;
      int cmp = -1;
      if (h) {
	 cmp = strcmp(fname, fn);
	 if (cmp < 0)
	    headerFree(h);
      }
      while (cmp < 0) {
	 h = headerRead(fd, HEADER_MAGIC_YES);
	 if (h == NULL) {
	    eof = 1;
	    return NULL;
	 }
	 fname = headerGetString(h, CRPMTAG_FILENAME);
	 if (fname == NULL) {
	    cerr << "gensrclist: bad input from stdin" << endl;
	    eof = 1;
	    return NULL;
	 }
	 cmp = strcmp(fname, fn);
	 if (cmp < 0)
	    headerFree(h);
      }
      if (cmp > 0)
	 return NULL;
      Header ret = NULL;
      if (stmatch(h, st))
	 ret = h;
      else
	 headerFree(h);
      h = NULL;
      return ret;
   }
};

#include "zhdr.h"

int main(int argc, char ** argv) 
{
   char buf[PATH_MAX];
   char cwd[PATH_MAX];
   string srpmdir;
   struct dirent **dirEntries;
   int i;
   int entry_no, entry_cur;
   bool mapi = false;
   bool progressBar = false;
   bool flatStructure = false;
   char *arg_dir, *arg_suffix, *arg_srpmindex;
   const char *srcListSuffix = NULL;
   bool srcListAppend = false;
   bool prevStdin = false;

   putenv((char *)"LC_ALL="); // Is this necessary yet (after i18n was supported)?
   for (i = 1; i < argc; i++) {
      if (strcmp(argv[i], "--mapi") == 0) {
	 mapi = true;
      } else if (strcmp(argv[i], "--flat") == 0) {
	 flatStructure = true;
      } else if (strcmp(argv[i], "--progress") == 0) {
	 progressBar = true;
      } else if (strcmp(argv[i], "--append") == 0) {
	 srcListAppend = true;
      } else if (strcmp(argv[i], "--meta") == 0) {
	 i++;
	 if (i < argc) {
	    srcListSuffix = argv[i];
	 } else {
	    cerr << "gensrclist: argument missing for option --meta"<<endl;
	    exit(1);
	 }
      } else if (strcmp(argv[i], "--cachedir") == 0) {
	 i++;
	 if (i < argc) {
            _config->Set("Dir::Cache", argv[i]);
	 } else {
            cerr << "genpkglist: argument missing for option --cachedir"<<endl;
	    exit(1);
	 }
      } else if (strcmp(argv[i], "--prev-stdin") == 0) {
	 prevStdin = true;
      } else {
	 break;
      }
   }
   if (argc - i == 3) {
      arg_dir = argv[i++];
      arg_suffix = argv[i++];
      arg_srpmindex = argv[i++];
   }
   else {
      usage();
      exit(1);
   }
   
   map<string, vector<const char *> > srpm2rpms;
   FILE *fp = fopen(arg_srpmindex, "r");
   if (fp == NULL) {
      cerr << "gensrclist: " << arg_srpmindex << ": " << strerror(errno) << endl;
      return 1;
   }
   readIndex(fp, srpm2rpms);
   fclose(fp);
   
   if(getcwd(cwd, PATH_MAX) == 0)
   {
      cerr << argv[0] << ": " << strerror(errno) << endl;
      exit(1);
   }
   
   if (*arg_dir != '/') {
      strcpy(buf, cwd);
      strcat(buf, "/");
      strcat(buf, arg_dir);
   } else
       strcpy(buf, arg_dir);
   
   strcat(buf, "/SRPMS.");
   strcat(buf, arg_suffix);
   
   srpmdir = "SRPMS." + string(arg_suffix);
#ifdef OLD_FLATSCHEME
   if (flatStructure) {
      // add the last component of the directory to srpmdir
      // that will cancel the effect of the .. used in sourcelist.cc
      // when building the directory from where to fetch srpms in apt
      char *prefix;
      prefix = strrchr(arg_dir, '/');
      if (prefix == NULL)
	 prefix = arg_dir;
      else
	 prefix++;
      if (*prefix != 0 && *(prefix+strlen(prefix)-1) == '/')
	 srpmdir = string(prefix) + srpmdir;
      else
	 srpmdir = string(prefix) + "/" + srpmdir;
   }
#else
   if (!flatStructure)
      srpmdir = "../"+srpmdir;
#ifndef REMOVE_THIS_SOMEDAY
   /* This code is here just so that code in rpmsrcrecords.cc in versions
    * prior to 0.5.15cnc4 is able to detect if that's a "new" style SRPM
    * directory scheme, or an old style. Someday, when 0.5.15cnc4 will be
    * history, this code may be safely removed. */
   else
      srpmdir = "./"+srpmdir;
#endif
#endif
   
   entry_no = scandir(buf, &dirEntries, selectRPMs, asciisort);
   if (entry_no < 0) { 
      cerr << "gensrclist: error opening directory " << buf << ": "
	  << strerror(errno) << endl;
      return 1;
   }

   if(chdir(buf) != 0)
   {
      cerr << argv[0] << ":" << strerror(errno) << endl;
      exit(1);
   }
   
   if (srcListSuffix != NULL)
      sprintf(buf, "%s/srclist.%s" ZHDR_SUFFIX, cwd, srcListSuffix);
   else
      sprintf(buf, "%s/srclist.%s" ZHDR_SUFFIX, cwd, arg_suffix);
   
   FD_t outfd;
   if (srcListAppend == true && FileExists(buf)) {
      outfd = Fopen(buf, "a");
   } else {
      unlink(buf);
      outfd = Fopen(buf, "w+");
   }
   if (!outfd) {
      cerr << "gensrclist: error creating file " << buf << ": "
	  << strerror(errno) << endl;
      return 1;
   }

   FD_t prevfd = NULL;
   if (prevStdin) {
      prevfd = fdDup(0);
      if (Ferror(prevfd)) {
	 cerr << "gensrclist: cannot open stdin" << endl;
	 return 1;
      }
   }
   HdlistReader prevhdlist(prevfd);

   CachedMD5 md5cache(string(arg_dir) + string(arg_suffix), "gensrclist");

   const size_t N_MERGE = 8;
   std::vector<Header> hh;
   hh.reserve(N_MERGE);

   for (entry_cur = 0; entry_cur < entry_no; entry_cur++) {

      if (progressBar)
	 simpleProgress(entry_cur + 1, entry_no);

      const char *fname = dirEntries[entry_cur]->d_name;

      struct stat sb;
      if (stat(fname, &sb) < 0) {
	 cerr << "gensrclist: " << fname << ": " << strerror(errno) << endl;
	 return 1;
      }

      Header h = NULL, newHeader = NULL;
      if (prevStdin)
	 newHeader = prevhdlist.find(fname, sb);
      if (newHeader == NULL)
	 h = readHeader(fname);
      if (h == NULL && newHeader == NULL) {
	 cerr << "gensrclist: " << fname << ": cannot read package header" << endl;
	 return 1;
      }

      if (!newHeader) {
	 newHeader = headerNew();
	 copyTags(h, newHeader, numTags, tags);
	 addAptTags(newHeader, srpmdir.c_str(), fname, sb.st_size);

	 char md5[34];
	 md5cache.MD5ForFile(fname, sb.st_mtime, md5);
	 headerPutString(newHeader, CRPMTAG_MD5, md5);
      }

      auto zWrite = [&]()
      {
	 hh.push_back(newHeader);
	 if (hh.size() == N_MERGE || entry_cur == entry_no - 1) {
	    size_t zsize;
	    void *zblob = zhdrv(hh, zsize);
	    Fwrite(zblob, zsize, 1, outfd);
	    free(zblob);
	    for (size_t i = 0; i < hh.size(); i++)
	       headerFree(hh[i]);
	    hh.clear();
	 }
      };

      map<string, vector<const char *> >::const_iterator I = srpm2rpms.find(fname);
      if (I != srpm2rpms.end()) {
	 const vector<const char *> &rpmv = I->second;
	 assert(rpmv.size() > 0);
	 headerPutStringArray(newHeader, CRPMTAG_BINARY, (const char **) &rpmv[0], rpmv.size());
	 zWrite();
      }
      else if (!mapi) { // write anyway
	 zWrite();
      }

      headerFree(h);
   } 
   
   Fclose(outfd);

   return 0;
}

// vim:sts=3:sw=3

/*
 * $Id: genpkglist.cc,v 1.7 2003/01/30 17:18:21 niemeyer Exp $
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
#include <set>
#include <iostream>
#include <fstream>

#include <apt-pkg/error.h>
#include <apt-pkg/tagfile.h>
#include <apt-pkg/configuration.h>

#include "rapt-compat.h"
#include "crpmtag.h"
#include "cached_md5.h"
#include "genutil.h"

raptTag tags[] =  {
       RPMTAG_NAME, 
       RPMTAG_EPOCH,
       RPMTAG_VERSION,
       RPMTAG_RELEASE,
       RPMTAG_GROUP,
       RPMTAG_ARCH,
       RPMTAG_PACKAGER,
       RPMTAG_SOURCERPM,
       RPMTAG_SIZE,
       RPMTAG_VENDOR,

       RPMTAG_DESCRIPTION,
       RPMTAG_SUMMARY,
       RPMTAG_BUILDTIME,
       /*RPMTAG_HEADERI18NTABLE*/ HEADER_I18NTABLE,

       RPMTAG_REQUIREFLAGS,
       RPMTAG_REQUIRENAME,
       RPMTAG_REQUIREVERSION,

       RPMTAG_CONFLICTFLAGS,
       RPMTAG_CONFLICTNAME,
       RPMTAG_CONFLICTVERSION,

       RPMTAG_PROVIDENAME,
       RPMTAG_PROVIDEFLAGS,
       RPMTAG_PROVIDEVERSION,

       RPMTAG_OBSOLETENAME,
       RPMTAG_OBSOLETEFLAGS,
       RPMTAG_OBSOLETEVERSION
};
int numTags = sizeof(tags) / sizeof(tags[0]);

static
void copyChangelog(Header h1, Header h2, unsigned since)
{
   struct rpmtd_s times, names, texts;
   if (headerGet(h1, RPMTAG_CHANGELOGTIME, &times, HEADERGET_MINMEM) != 1)
      return;
   if (headerGet(h1, RPMTAG_CHANGELOGNAME, &names, HEADERGET_MINMEM) != 1) {
      rpmtdFreeData(&times);
      return;
   }
   if (headerGet(h1, RPMTAG_CHANGELOGTEXT, &texts, HEADERGET_MINMEM) != 1) {
      rpmtdFreeData(&times);
      rpmtdFreeData(&names);
      return;
   }
   if (times.count != names.count || names.count != texts.count) {
exit: rpmtdFreeData(&times);
      rpmtdFreeData(&names);
      rpmtdFreeData(&texts);
      return;
   }

   int n = times.count; // in
   int m = 0; // out

   if (n < 1)
      goto exit;

   unsigned *time = (unsigned *) times.data;

   for (int i = 0; i < n; i++)
      if (time[i] >= since)
	 m++;
      else
	 break;
   if (m < n)
      m++;

   headerPutUint32(h2, RPMTAG_CHANGELOGTIME, (const unsigned *) times.data, m);
   headerPutStringArray(h2, RPMTAG_CHANGELOGNAME, (const char **) names.data, m);
   headerPutStringArray(h2, RPMTAG_CHANGELOGTEXT, (const char **) texts.data, m);
   goto exit;
}


static
bool usefulFile(const char *d, const char *b,
	       const set<string> &depFiles)
{
   // PATH-like directories
   if (endswith(d, "/bin/") || endswith(d, "/sbin/"))
      return true;
   if (strcmp(d, "/usr/games/") == 0)
      return true;
   if (strcmp(d, "/usr/lib/kde4bin/") == 0)
      return true;
   // Java jars
   if (startswith(d, "/usr/share/java/") && endswith(b, ".jar"))
      return true;
   // ttf and otf fonts
   if (startswith(d, "/usr/share/fonts/") && (endswith(b, ".ttf") || endswith(b, ".otf")))
      return true;

   // shared libraries
   if (strncmp(b, "lib", 3) == 0 && strstr(b + 3, ".so"))
      return true;

   // required by other packages
   if (depFiles.find(string(d) + b) != depFiles.end()) {
      // fprintf(stderr, "useful depfile: %s%s\n", d, b);
      return true;
   }

   return false;
}


static
void copyStrippedFileList(Header h1, Header h2,
			  const set<string> &depFiles)
{
   struct {
      raptTagCount bnc, dnc, dic;
      const char **bn, **dn;
      uint32_t *di;
   }
   l1 = {0}, l2 = {0};

   struct rpmtd_s bnames, dnames, didexes;
   int rc;

   rc = headerGet(h1, RPMTAG_BASENAMES, &bnames, HEADERGET_MINMEM);
   if (rc != 1)
      return;
   assert(rpmtdType(&bnames) == RPM_STRING_ARRAY_TYPE);
   assert(rpmtdCount(&bnames) > 0);

   l1.bn = (const char**) bnames.data;
   l1.bnc = bnames.count;

   rc = headerGet(h1, RPMTAG_DIRNAMES, &dnames, HEADERGET_MINMEM);
   assert(rc == 1);
   assert(rpmtdType(&dnames) == RPM_STRING_ARRAY_TYPE);

   l1.dn = (const char**) dnames.data;
   l1.dnc = dnames.count;

   rc = headerGet(h1, RPMTAG_DIRINDEXES, &didexes, HEADERGET_MINMEM);
   assert(rc == 1);
   assert(rpmtdType(&didexes) == RPM_INT32_TYPE);

   l1.di = (uint32_t *) didexes.data;
   l1.dic = didexes.count;

   assert(l1.bnc == l1.dic);

   for (int i = 0; i < l1.bnc; i++)
   {
      const char *d = l1.dn[l1.di[i]];
      const char *b = l1.bn[i];

      if (!usefulFile(d, b, depFiles))
	 continue;

      if (l2.bnc == 0) {
         l2.bn = new const char *[l1.bnc];
         l2.dn = new const char *[l1.dnc];
         l2.di = new uint32_t[l1.dic];
      }

      l2.bn[l2.bnc++] = b;

      bool has_dir = false;
      for (int j = 0; j < l2.dnc; j++) {
         if (l2.dn[j] == d) {
            l2.di[l2.dic++] = j;
            has_dir = true;
            break;
         }
      }
      if (!has_dir) {
         l2.dn[l2.dnc] = d;
         l2.di[l2.dic++] = l2.dnc++;
      }
   }

   assert(l2.bnc == l2.dic);

   if (l2.bnc > 0) {
      headerPutStringArray(h2, RPMTAG_BASENAMES, l2.bn, l2.bnc);
      headerPutStringArray(h2, RPMTAG_DIRNAMES, l2.dn, l2.dnc);
      headerPutUint32(h2, RPMTAG_DIRINDEXES, l2.di, l2.dic);
      delete[] l2.bn;
      delete[] l2.dn;
      delete[] l2.di;
   }

   rpmtdFreeData(&bnames);
   rpmtdFreeData(&dnames);
   rpmtdFreeData(&didexes);
}


static
void findDepFiles(Header h, set<string> &depFiles, raptTag tag)
{
   struct rpmtd_s td;
   int rc = headerGet(h, tag, &td, HEADERGET_MINMEM);
   if (rc != 1)
      return;
   assert(rpmtdType(&td) == RPM_STRING_ARRAY_TYPE);
   const char **deps = (const char **) td.data;
   for (rpm_count_t i = 0; i < td.count; i++) {
      const char *dep = deps[i];
      if (*dep == '/')
         depFiles.insert(dep);
   }
   rpmtdFreeData(&td);
}


typedef struct {
   string importance;
   string date;
   string summary;
   string url;
} UpdateInfo;


static
bool loadUpdateInfo(char *path, map<string,UpdateInfo> &M)
{
   FileFd F(path, FileFd::ReadOnly);
   if (_error->PendingError()) 
   {
      return false;
   }
   
   pkgTagFile Tags(&F);
   pkgTagSection Section;
   
   while (Tags.Step(Section)) 
   {
      string file = Section.FindS("File");
      UpdateInfo info;

      info.importance = Section.FindS("Importance");
      info.date = Section.FindS("Date");
      info.summary = Section.FindS("Summary");
      info.url = Section.FindS("URL");

      M[file] = info;
   }
   return true;
}


static
void addInfoTags(Header h, const char *fname,
		 const map<string,UpdateInfo> &M)
{
   map<string,UpdateInfo>::const_iterator I = M.find(fname);
   if (I == M.end())
      return;
   const UpdateInfo &info = I->second;
   headerPutString(h, CRPMTAG_UPDATE_SUMMARY, info.summary.c_str());
   headerPutString(h, CRPMTAG_UPDATE_URL, info.url.c_str());
   headerPutString(h, CRPMTAG_UPDATE_DATE, info.date.c_str());
   headerPutString(h, CRPMTAG_UPDATE_IMPORTANCE, info.importance.c_str());
}


void usage()
{
   cerr << "genpkglist " << VERSION << endl;
   cerr << "usage: genpkglist [<options>] <dir> <suffix>" << endl;
   cerr << "options:" << endl;
   cerr << " --index <file>  file to write srpm index data to" << endl;
   cerr << " --info <file>   file to read update info from" << endl;
   cerr << " --useful-files <file>  file to read the list of useful files from" << endl;
   cerr << " --meta <suffix> create package file list with given suffix" << endl;
   cerr << " --no-scan       do not scan for useful files" << endl;
   cerr << " --bloat         do not strip the package file list. Needed for some" << endl;
   cerr << "                 distributions that use non-automatically generated" << endl;
   cerr << "                 file dependencies" << endl;
   cerr << " --append        append to the package file list, don't overwrite" << endl;
   cerr << " --progress      show a progress bar" << endl;
   cerr << " --cachedir=DIR  use a custom directory for package md5sum cache"<<endl;
   cerr << " --changelog-since <seconds>" <<endl;
   cerr << "                 save package changelogs; copy changelog entries" <<endl;
   cerr << "                 newer than seconds since the Epoch, and also" <<endl;
   cerr << "                 one preceding entry (if any)" <<endl;
}


// A group is one or more headers, grouped by their source rpm, furthermore
// in the right output order (adjacent, sorted by rpm filename), compressed
// in a single chunk.  The overall genpkglist algorithm is to load the groups,
// sort them by source rpm, and finally to spew them out.
struct group {
   const char *rpm; // the first rpm in a group, for collation
   const char *srpm;
   void *zblob;
   size_t zsize;
};

static
int groupCmp(const void *g1_, const void *g2_)
{
   const struct group *g1 = (const struct group *) g1_;
   const struct group *g2 = (const struct group *) g2_;
   int cmp = strcmp(g1->srpm, g2->srpm);
   if (cmp)
      return cmp;
   return strcmp(g1->rpm, g2->rpm);
}

#include <vector>
#include "zhdr.h"
#include "slab.h"

int main(int argc, char ** argv) 
{
   string rpmsdir;
   string pkglist_path;
   struct dirent **dirEntries;
   int entry_no, entry_cur;
   char *op_dir;
   char *op_suf;
   char *op_index = NULL;
   char *op_usefulFiles = NULL;
   char *op_update = NULL;
   int i;
   long /* time_t */ changelog_since = 0;
   bool fullFileList = false;
   bool noScan = false;
   bool progressBar = false;
   const char *pkgListSuffix = NULL;
   bool pkgListAppend = false;
   
   putenv((char *)"LC_ALL="); // Is this necessary yet (after i18n was supported)?
   for (i = 1; i < argc; i++) {
      if (strcmp(argv[i], "--index") == 0) {
	 i++;
	 if (i < argc) {
	    op_index = argv[i];
	 } else {
	    cerr << "genpkglist: filename missing for option --index"<<endl;
	    exit(1);
	 }
      } else if (strcmp(argv[i], "--info") == 0) {
	 i++;
	 if (i < argc) {
	    op_update = argv[i];
	 } else {
	    cerr << "genpkglist: filename missing for option --info"<<endl;
	    exit(1);
	 }
      } else if (strcmp(argv[i], "--useful-files") == 0) {
	 i++;
	 if (i < argc) {
	    op_usefulFiles = argv[i];
	 } else {
	    cerr << "genpkglist: filename missing for option --useful-files"<<endl;
	    return 1;
	 }
      } else if (strcmp(argv[i], "--changelog-since") == 0) {
	 i++;
	 if (i < argc) {
	    changelog_since = atol(argv[i]);
	 } else {
	    cerr << "genpkglist: argument missing for option --changelog-since" <<endl;
	    exit(1);
	 }
      } else if (strcmp(argv[i], "--bloat") == 0) {
	 fullFileList = true;
      } else if (strcmp(argv[i], "--no-scan") == 0) {
	 noScan = true;
      } else if (strcmp(argv[i], "--progress") == 0) {
	 progressBar = true;
      } else if (strcmp(argv[i], "--append") == 0) {
	 pkgListAppend = true;
      } else if (strcmp(argv[i], "--meta") == 0) {
	 i++;
	 if (i < argc) {
	    pkgListSuffix = argv[i];
	 } else {
	    cerr << "genpkglist: argument missing for option --meta"<<endl;
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
      } else {
	 break;
      }
   }
   if (argc - i > 0)
       op_dir = argv[i++];
   else {
      usage();
      exit(1);
   }
   if (argc - i > 0)
       op_suf = argv[i++];
   else {
      usage();
      exit(1);
   }
   if (argc != i) {
      usage();
   }
   
   map<string,UpdateInfo> updateInfo;
   if (op_update) {
      if (!loadUpdateInfo(op_update, updateInfo)) {
	 cerr << "genpkglist: error reading update info from file " << op_update << endl;
	 _error->DumpErrors();
	 exit(1);
      }
   }

   FILE *idxfp = NULL;
   if (op_index) {
      idxfp = fopen(op_index, "w+");
      if (!idxfp) {
	 cerr << "genpkglist: could not open " << op_index << " for writing";
	 perror("");
	 exit(1);
      }
   }

   {
      char cwd[PATH_MAX];
      
      if (getcwd(cwd, PATH_MAX) == 0)
      {
         cerr << argv[0] << ": " << strerror(errno) << endl;
         exit(1);
      }
      if (*op_dir != '/') {
	 rpmsdir = string(cwd) + "/" + string(op_dir);
      } else {
	 rpmsdir = string(op_dir);
      }
   }
   pkglist_path = string(rpmsdir);
   rpmsdir = rpmsdir + "/RPMS." + string(op_suf);

   string dirtag = "RPMS." + string(op_suf);

   if (chdir(rpmsdir.c_str()) != 0 ||
	 (entry_no = scandir(".", &dirEntries, selectRPMs, asciisort)) < 0)
   {
      cerr << "genpkglist: " << rpmsdir << ": " << strerror(errno) << endl;
      return 1;
   }

   if (pkgListSuffix != NULL)
      pkglist_path = pkglist_path + "/base/pkglist." + pkgListSuffix + ZHDR_SUFFIX;
   else
      pkglist_path = pkglist_path + "/base/pkglist." + op_suf + ZHDR_SUFFIX;
   
   FD_t outfd;
   if (pkgListAppend == true && FileExists(pkglist_path)) {
      outfd = Fopen(pkglist_path.c_str(), "a");
   } else {
      unlink(pkglist_path.c_str());
      outfd = Fopen(pkglist_path.c_str(), "w+");
   }
   if (!outfd) {
      cerr << "genpkglist: error creating file " << pkglist_path << ": "
	  << strerror(errno) << endl;
      return 1;
   }

   set<string> usefulFiles;
   if (op_usefulFiles) {
      ifstream strm(op_usefulFiles);
      if (!strm) {
	 cerr << "genpkglist: cannot open " << op_usefulFiles <<endl;
	 return 1;
      }
      string line;
      while (std::getline(strm, line))
	 usefulFiles.insert(line);
   }

   CachedMD5 md5cache(string(op_dir) + string(op_suf), "genpkglist");

   auto processHeader = [&](Header h, const char *rpm, bool bloat)
   {
      struct stat sb;
      int statrc = stat(rpm, &sb);
      assert(statrc == 0);

      Header newHeader = headerNew();
      assert(newHeader);
      copyTags(h, newHeader, numTags, tags);
      if (!bloat)
	 copyStrippedFileList(h, newHeader, usefulFiles);
      else {
	 copyTag(h, newHeader, RPMTAG_BASENAMES);
	 copyTag(h, newHeader, RPMTAG_DIRNAMES);
	 copyTag(h, newHeader, RPMTAG_DIRINDEXES);
      }
      if (changelog_since > 0)
	 copyChangelog(h, newHeader, changelog_since);

      addAptTags(newHeader, dirtag.c_str(), rpm, sb.st_size);
      if (op_update)
	 addInfoTags(newHeader, rpm, updateInfo);

      char md5[34];
      md5cache.MD5ForFile(rpm, sb.st_mtime, md5);
      headerPutString(newHeader, CRPMTAG_MD5, md5);

      if (idxfp) {
	 const char *srpm = headerGetString(h, RPMTAG_SOURCERPM);
	 const char *name = headerGetString(h, RPMTAG_NAME);
	 if (srpm && name)
	    fprintf(idxfp, "%s %s\n", srpm, name);
      }
      return newHeader;
   };

   Slab slab;

   struct group *groups = NULL;
   int ngroup = 0;

   if (entry_no > 0)
      groups = new struct group[entry_no];

   // a few headers grouped by SOURCERPM
   std::vector<Header> hh;
   hh.reserve(128);

   // load the groups
   for (entry_cur = 0; entry_cur < entry_no; entry_cur++) {

      if (progressBar)
	 simpleProgress(entry_cur + 1, entry_no);

      const char *rpm = dirEntries[entry_cur]->d_name;

      Header h = readHeader(rpm);
      if (h == NULL) {
	 cerr << "genpkglist: " << rpm << ": cannot read package header" << endl;
	 return 1;
      }

      const char *srpm = headerGetString(h, RPMTAG_SOURCERPM);
      if (srpm == NULL) {
	 cerr << "genpkglist: " << rpm << ": invalid binary package" << endl;
	 headerFree(h);
	 return 1;
      }

      if (!(fullFileList || noScan)) {
	 findDepFiles(h, usefulFiles, RPMTAG_REQUIRENAME);
	 findDepFiles(h, usefulFiles, RPMTAG_PROVIDENAME);
	 findDepFiles(h, usefulFiles, RPMTAG_CONFLICTNAME);
	 findDepFiles(h, usefulFiles, RPMTAG_OBSOLETENAME);

	 // headers will be reloaded in the second pass, make simple 1-element
	 // groups (actual grouping is only detected to avoid slab.strdup)
	 bool group = ngroup && strcmp(groups[ngroup-1].srpm, srpm) == 0;
	 srpm = group ? groups[ngroup-1].srpm : slab.strdup(srpm);
	 headerFree(h);
	 groups[ngroup++] = (struct group) { .rpm = rpm, .srpm = srpm,
					     .zblob = NULL, .zsize = 0 };
	 continue;
      }

      Header newHeader = processHeader(h, rpm, fullFileList);
      // see if there is a grouping (ngroup works here more like gi:
      // groups[ngroup].srpm must exist, except for the very beginning;
      // ngroup is only increased after the group is finished)
      bool group = entry_cur && strcmp(groups[ngroup].srpm, srpm) == 0;
      srpm = group ? groups[ngroup].srpm : slab.strdup(srpm);
      headerFree(h);

      // how to merge the group
      auto mergeGroup = [&]()
      {
	 void *zblob = zhdrv(hh, groups[ngroup].zsize);
	 groups[ngroup].zblob = slab.put(zblob, groups[ngroup].zsize);
	 free(zblob);
	 for (size_t i = 0; i < hh.size(); i++)
	    headerFree(hh[i]);
	 hh.clear();
	 ngroup++;
      };
      // add the previous group to groups
      if (!group && hh.size())
	 mergeGroup();
      // start a new group
      if (hh.size() == 0) {
	 groups[ngroup].rpm = rpm;
	 groups[ngroup].srpm = srpm;
      }
      // add to group
      hh.push_back(newHeader);
      // flush on the last iteration
      if (entry_cur == entry_no - 1)
	 mergeGroup();
   }

   if (ngroup > 1)
      qsort(groups, ngroup, sizeof(groups[0]), groupCmp);

   for (int gi = 0; gi < ngroup; gi++) {

      if (fullFileList || noScan) {
	 // only left to write
	 Fwrite(groups[gi].zblob, groups[gi].zsize, 1, outfd);
	 continue;
      }

      if (progressBar)
	 simpleProgress(gi + 1, ngroup);

      // may need postprocessing, due to stripped file lists
      // (it seems that they cannot be easily replaced)
      auto postproc = [&](Header h)
      {
	 Header newHeader = headerNew();
	 copyTags(h, newHeader, numTags, tags);
	 raptTag moreTags[] = {
	    // changelog
	    RPMTAG_CHANGELOGTIME,
	    RPMTAG_CHANGELOGNAME,
	    RPMTAG_CHANGELOGTEXT,
	    // apt tags
	    CRPMTAG_DIRECTORY,
	    CRPMTAG_FILENAME,
	    CRPMTAG_FILESIZE,
	    CRPMTAG_MD5,
	    // info tags
	    CRPMTAG_UPDATE_SUMMARY,
	    CRPMTAG_UPDATE_URL,
	    CRPMTAG_UPDATE_DATE,
	    CRPMTAG_UPDATE_IMPORTANCE,
	 };
	 copyTags(h, newHeader, numTags, moreTags);
	 copyStrippedFileList(h, newHeader, usefulFiles);
	 headerFree(h);
	 return newHeader;
      };

      // merge writes directly to outfd
      auto mergeGroup = [&]()
      {
	 size_t zsize;
	 void *zblob = zhdrv(hh, zsize);
	 Fwrite(zblob, zsize, 1, outfd);
	 free(zblob);
	 for (size_t i = 0; i < hh.size(); i++)
	    headerFree(hh[i]);
	 hh.clear();
      };

      // can coalesce a few groups, preferably with the same sourcerpm
      bool group = gi && strcmp(groups[gi-1].srpm, groups[gi].srpm) == 0;
      if (!group && hh.size() > 1)
	 mergeGroup();
      if (groups[gi].zblob == NULL) {
	 const char *rpm = groups[gi].rpm;
	 Header h = readHeader(rpm);
	 if (h == NULL) {
	    cerr << "genpkglist: " << rpm << ": cannot read package header" << endl;
	    return 1;
	 }
	 Header newHeader = processHeader(h, rpm, fullFileList);
	 headerFree(h);
	 hh.push_back(newHeader);
      } else {
	 // this branch is not taken yet
	 bool pp = false;
	 assert(pp);
	 size_t i = hh.size();
	 unzhdrv(hh, groups[gi].zblob, groups[gi].zsize);
	 for (; i < hh.size(); i++)
	    hh[i] = postproc(hh[i]);
      }
      if (gi == ngroup - 1)
	 mergeGroup();
   }
#if 0
   system("ps up $PPID");
#endif
   Fclose(outfd);

   return 0;
}

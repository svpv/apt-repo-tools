/*
 * $Id: genpkglist.cc,v 1.7 2003/01/30 17:18:21 niemeyer Exp $
 */
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
#include <config.h>

#include "rpmhandler.h"
#include "cached_md5.h"
#include "genutil.h"

#define CRPMTAG_TIMESTAMP   1012345

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
       RPMTAG_OS,
       
       RPMTAG_DESCRIPTION, 
       RPMTAG_SUMMARY, 
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
bool usefulFile(const char *d, const char *b,
	       const set<string> &depFiles)
{
   // PATH-like directories
   if (endswith(d, "/bin/") || endswith(d, "/sbin/"))
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
   raptTagType bnt, dnt, dit;
   struct {
      raptTagCount bnc, dnc, dic;
      const char **bn, **dn;
      raptInt *di;
   }
   l1 = {0}, l2 = {0};

   int rc;
   rc = headerGetEntry(h1, RPMTAG_BASENAMES, &bnt, (void**)&l1.bn, &l1.bnc);
   if (rc != 1)
      return;
   assert(bnt == RPM_STRING_ARRAY_TYPE);
   assert(l1.bnc > 0);

   rc = headerGetEntry(h1, RPMTAG_DIRNAMES, &dnt, (void**)&l1.dn, &l1.dnc);
   assert(rc == 1);
   assert(dnt == RPM_STRING_ARRAY_TYPE);

   rc = headerGetEntry(h1, RPMTAG_DIRINDEXES, &dit, (void**)&l1.di, &l1.dic);
   assert(rc == 1);
   assert(dit == RPM_INT32_TYPE);
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
         l2.di = new raptInt[l1.dic];
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
      headerAddEntry(h2, RPMTAG_BASENAMES, bnt, l2.bn, l2.bnc);
      headerAddEntry(h2, RPMTAG_DIRNAMES, dnt, l2.dn, l2.dnc);
      headerAddEntry(h2, RPMTAG_DIRINDEXES, dit, l2.di, l2.dic);
      delete[] l2.bn;
      delete[] l2.dn;
      delete[] l2.di;
   }

   headerFreeData(l1.bn, (rpmTagType)bnt);
   headerFreeData(l1.dn, (rpmTagType)dnt);
   headerFreeData(l1.di, (rpmTagType)dit);
}


static
void findDepFiles(Header h, set<string> &depFiles, raptTag tag)
{
   raptTagType type;
   raptTagCount count;
   raptTagData data;
   int rc = headerGetEntry(h, tag, &type, &data, &count);
   if (rc != 1)
      return;
   assert(type == RPM_STRING_ARRAY_TYPE);
   const char **deps = (const char **) data;
   for (int i = 0; i < count; i++) {
      const char *dep = deps[i];
      if (*dep == '/')
	 depFiles.insert(dep);
   }
   headerFreeData(data, (rpmTagType)type);
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
   addStringTag(h, CRPMTAG_UPDATE_SUMMARY, info.summary.c_str());
   addStringTag(h, CRPMTAG_UPDATE_URL, info.url.c_str());
   addStringTag(h, CRPMTAG_UPDATE_DATE, info.date.c_str());
   addStringTag(h, CRPMTAG_UPDATE_IMPORTANCE, info.importance.c_str());
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
   cerr << " --bloat         do not strip the package file list. Needed for some" << endl;
   cerr << "                 distributions that use non-automatically generated" << endl;
   cerr << "                 file dependencies" << endl;
   cerr << " --append        append to the package file list, don't overwrite" << endl;
   cerr << " --progress      show a progress bar" << endl;
   cerr << " --cachedir=DIR  use a custom directory for package md5sum cache"<<endl;
}


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
   bool fullFileList = false;
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
	    cout << "genpkglist: filename missing for option --index"<<endl;
	    exit(1);
	 }
      } else if (strcmp(argv[i], "--info") == 0) {
	 i++;
	 if (i < argc) {
	    op_update = argv[i];
	 } else {
	    cout << "genpkglist: filename missing for option --info"<<endl;
	    exit(1);
	 }
      } else if (strcmp(argv[i], "--useful-files") == 0) {
	 i++;
	 if (i < argc) {
	    op_usefulFiles = argv[i];
	 } else {
	    cout << "genpkglist: filename missing for option --useful-files"<<endl;
	    return 1;
	 }
      } else if (strcmp(argv[i], "--bloat") == 0) {
	 fullFileList = true;
      } else if (strcmp(argv[i], "--progress") == 0) {
	 progressBar = true;
      } else if (strcmp(argv[i], "--append") == 0) {
	 pkgListAppend = true;
      } else if (strcmp(argv[i], "--meta") == 0) {
	 i++;
	 if (i < argc) {
	    pkgListSuffix = argv[i];
	 } else {
	    cout << "genpkglist: argument missing for option --meta"<<endl;
	    exit(1);
	 }
      } else if (strcmp(argv[i], "--cachedir") == 0) {
	 i++;
	 if (i < argc) {
            _config->Set("Dir::Cache", argv[i]);
	 } else {
            cout << "genpkglist: argument missing for option --cachedir"<<endl;
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

   entry_no = scandir(rpmsdir.c_str(), &dirEntries, selectRPMs, alphasort);
   if (entry_no < 0) {
      cerr << "genpkglist: error opening directory " << rpmsdir << ": "
	  << strerror(errno) << endl;
      return 1;
   }
   
   if (chdir(rpmsdir.c_str()) != 0)
   {
      cerr << argv[0] << ": " << strerror(errno) << endl;
      return 1;
   }
   
   if (pkgListSuffix != NULL)
	   pkglist_path = pkglist_path + "/base/pkglist." + pkgListSuffix;
   else
	   pkglist_path = pkglist_path + "/base/pkglist." + op_suf;
   
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
	 cout << "genpkglist: cannot open " << op_usefulFiles <<endl;
	 return 1;
      }
      string line;
      while (std::getline(strm, line))
	 usefulFiles.insert(line);
   }

   if (!fullFileList)
   // File list cannot be stripped in a dumb manner - this is going
   // unmet dependencies.  First pass is required to find depFiles.
   for (entry_cur = 0; entry_cur < entry_no; entry_cur++) {

      if (progressBar)
	 simpleProgress(entry_cur + 1, entry_no);

      const char *fname = dirEntries[entry_cur]->d_name;

      struct stat sb;
      if (stat(fname, &sb) < 0) {
	 cerr << "Warning: " << fname << ": " << strerror(errno) << endl;
	 continue;
      }

      Header h = readHeader(fname);
      if (h == NULL) {
	 cerr << "Warning: " << fname << ": cannot read package header" << endl;
	 continue;
      }

      findDepFiles(h, usefulFiles, RPMTAG_REQUIRENAME);
      findDepFiles(h, usefulFiles, RPMTAG_PROVIDENAME);
      findDepFiles(h, usefulFiles, RPMTAG_CONFLICTNAME);
      findDepFiles(h, usefulFiles, RPMTAG_OBSOLETENAME);
   }

   CachedMD5 md5cache(string(op_dir) + string(op_suf), "genpkglist");

   for (entry_cur = 0; entry_cur < entry_no; entry_cur++) {

      if (progressBar)
	 simpleProgress(entry_cur + 1, entry_no);

      const char *fname = dirEntries[entry_cur]->d_name;

      struct stat sb;
      if (stat(fname, &sb) < 0) {
	 cerr << "Warning: " << fname << ": " << strerror(errno) << endl;
	 continue;
      }

      Header h = readHeader(fname);
      if (h == NULL) {
	 cerr << "Warning: " << fname << ": cannot read package header" << endl;
	 continue;
      }

      Header newHeader = headerNew();
      copyTags(h, newHeader, numTags, tags);
      if (!fullFileList)
	 copyStrippedFileList(h, newHeader, usefulFiles);
      else {
	 copyTag(h, newHeader, RPMTAG_BASENAMES);
	 copyTag(h, newHeader, RPMTAG_DIRNAMES);
	 copyTag(h, newHeader, RPMTAG_DIRINDEXES);
      }
      addAptTags(newHeader, dirtag.c_str(), fname, sb.st_size);
      if (op_update)
	 addInfoTags(newHeader, fname, updateInfo);

      char md5[34];
      md5cache.MD5ForFile(fname, sb.st_mtime, md5);
      addStringTag(newHeader, CRPMTAG_MD5, md5);

      if (idxfp) {
	 const char *srpm = getStringTag(h, RPMTAG_SOURCERPM);
	 const char *name = getStringTag(h, RPMTAG_NAME);
	 if (srpm && name)
	    fprintf(idxfp, "%s %s\n", srpm, name);
      }

      headerWrite(outfd, newHeader, HEADER_MAGIC_YES);

      headerFree(newHeader);
      headerFree(h);
   }

   Fclose(outfd);

   return 0;
}

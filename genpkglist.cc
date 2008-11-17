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
#include <iostream>

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
int usefulFile(const char *d, const char *b)
{
   // PATH-like directories
   if (endswith(d, "/bin/") || endswith(d, "/sbin/"))
      return 1;

   // shared libraries
   if (strncmp(b, "lib", 3) == 0 && strstr(b + 3, ".so"))
      return 1;

   return 0;
}


static void copyStrippedFileList(Header header, Header newHeader)
{
   raptTagCount i, i1, i2;
   
   raptTagType type1, type2, type3;
   raptTagCount count1, count2, count3;
   char **dirnames = NULL, **basenames = NULL;
   raptInt *dirindexes = NULL;
   raptTagData dirnameval = NULL, basenameval = NULL, dirindexval = NULL;
   char **dnames, **bnames;
   raptInt *dindexes;
   int res1, res2, res3;
   
#define FREE(a) if (a) free(a);
   
   res1 = headerGetEntry(header, RPMTAG_DIRNAMES, &type1, 
			 &dirnameval, &count1);
   res2 = headerGetEntry(header, RPMTAG_BASENAMES, &type2, 
			 &basenameval, &count2);
   res3 = headerGetEntry(header, RPMTAG_DIRINDEXES, &type3, 
			 &dirindexval, &count3);
   dirnames = (char **)dirnameval;
   basenames = (char **)basenameval;
   dirindexes = (raptInt *)dirindexval;
   
   if (res1 != 1 || res2 != 1 || res3 != 1) {
      FREE(dirnames);
      FREE(basenames);
      return;
   }

   dnames = dirnames;
   bnames = basenames;
   dindexes = (raptInt*)malloc(sizeof(raptInt)*count3);
   
   i1 = 0;
   i2 = 0;
   for (i = 0; i < count2 ; i++) 
   {
      int ok = usefulFile(dirnames[dirindexes[i]], basenames[i]);
      
      if (!ok) {
	 int k = i;
	 while (dirindexes[i] == dirindexes[k] && i < count2)
	     i++;
	 i--;
	 continue;
      }
      
      
      if (ok)
      {
	 raptTagCount j;
	 
	 bnames[i1] = basenames[i];
	 for (j = 0; j < i2; j++)
	 {
	    if (dnames[j] == dirnames[dirindexes[i]])
	    {
	       dindexes[i1] = j;
	       break;
	    }
	 }
	 if (j == i2) 
	 {
	    dnames[i2] = dirnames[dirindexes[i]];
	    dindexes[i1] = i2;
	    i2++;
	 }
	 assert(i2 <= count1);
	 i1++;
      } 
   }
   
   if (i1 == 0) {
      FREE(dirnames);
      FREE(basenames);
      FREE(dindexes);
      return;
   }
   
   headerAddEntry(newHeader, RPMTAG_DIRNAMES, type1, dnames, i2);
   
   headerAddEntry(newHeader, RPMTAG_BASENAMES, type2, bnames, i1);
   
   headerAddEntry(newHeader, RPMTAG_DIRINDEXES, type3, dindexes, i1);
   
   FREE(dirnames);
   FREE(basenames);
   FREE(dindexes);
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


bool copyFields(Header h, Header newHeader,
		FILE *idxfile, const char *directory, const char *filename,
		unsigned filesize, map<string,UpdateInfo> &updateInfo,
		bool fullFileList)
{
   if (fullFileList) {
      raptTagType type1, type2, type3;
      raptTagCount count1, count2, count3;
      char **dnames, **bnames, **dindexes;
      raptTagData dnameval, bnameval, dindexval;
      int res;
   
      res = headerGetEntry(h, RPMTAG_DIRNAMES, &type1, 
			   &dnameval, &count1);
      res = headerGetEntry(h, RPMTAG_BASENAMES, &type2, 
			   &bnameval, &count2);
      res = headerGetEntry(h, RPMTAG_DIRINDEXES, &type3, 
			   &dindexval, &count3);

      dnames = (char **)dnameval;
      bnames = (char **)bnameval;
      dindexes = (char **)dindexval;

      if (res == 1) {
	 headerAddEntry(newHeader, RPMTAG_DIRNAMES, type1, dnames, count1);
	 headerAddEntry(newHeader, RPMTAG_BASENAMES, type2, bnames, count2);
	 headerAddEntry(newHeader, RPMTAG_DIRINDEXES, type3, dindexes, count3);
      }
   } else {
       copyStrippedFileList(h, newHeader);
   }
   
   // update index of srpms
   if (idxfile) {
      raptTagType type;
      raptTagCount count;
      raptTagData srpmval, nameval;
      char *srpm, *name;
      int res;
      
      res = headerGetEntry(h, RPMTAG_NAME, &type, 
			   &nameval, &count);
      res = headerGetEntry(h, RPMTAG_SOURCERPM, &type, 
			   &srpmval, &count);
      name = (char *)nameval;
      srpm = (char *)srpmval;

      if (res == 1) {
	 fprintf(idxfile, "%s %s\n", srpm, name);
      }
   }

   return true;
}


void usage()
{
   cerr << "genpkglist " << VERSION << endl;
   cerr << "usage: genpkglist [<options>] <dir> <suffix>" << endl;
   cerr << "options:" << endl;
   cerr << " --index <file>  file to write srpm index data to" << endl;
   cerr << " --info <file>   file to read update info from" << endl;
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
   char *op_update = NULL;
   FILE *idxfile;
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
   if (op_index) {
      idxfile = fopen(op_index, "w+");
      if (!idxfile) {
	 cerr << "genpkglist: could not open " << op_index << " for writing";
	 perror("");
	 exit(1);
      }
   } else {
      idxfile = NULL;
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
      addAptTags(newHeader, dirtag.c_str(), fname, sb.st_size);
      if (op_update)
	 addInfoTags(newHeader, fname, updateInfo);

      copyFields(h, newHeader, idxfile, dirtag.c_str(), fname,
		 sb.st_size, updateInfo, fullFileList);

      char md5[34];
      md5cache.MD5ForFile(fname, sb.st_mtime, md5);
      addStringTag(newHeader, CRPMTAG_MD5, md5);

      headerWrite(outfd, newHeader, HEADER_MAGIC_YES);

      headerFree(newHeader);
      headerFree(h);
   }

   Fclose(outfd);

   return 0;
}

/* SPDX-License-Identifier: BSD-3-Clause */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#define DIR FATFS_DIR
#include "ff.h"
#undef DIR
#include "compat/dirent.h"
#include "pico/stdlib.h"
struct native_dir {FATFS_DIR fat;struct dirent entry;};
static int ff_error(FRESULT r){errno=r==FR_NO_FILE||r==FR_NO_PATH?ENOENT:r==FR_EXIST?EEXIST:r==FR_DENIED?EACCES:EIO;return -1;}
char *realpath(const char *name,char *resolved){
    if(!name||!*name){errno=ENOENT;return NULL;}
    char input[512],normalized[512];int n;
    if(name[0]=='/')n=snprintf(input,sizeof input,"%s",name);
    else{char dir[256];if(!getcwd(dir,sizeof dir))return NULL;n=snprintf(input,sizeof input,"%s/%s",dir,name);}
    if(n<0||n>=sizeof input){errno=ENAMETOOLONG;return NULL;}
    normalized[0]='/';normalized[1]=0;size_t len=1;
    char *save=NULL;
    for(char *part=strtok_r(input,"/",&save);part;part=strtok_r(NULL,"/",&save)){
        if(!strcmp(part,"."))continue;
        if(!strcmp(part,"..")){while(len>1&&normalized[--len]!='/');normalized[len]=0;continue;}
        size_t count=strlen(part);if(len+count+2>=sizeof normalized){errno=ENAMETOOLONG;return NULL;}
        if(len>1)normalized[len++]='/';memcpy(normalized+len,part,count+1);len+=count;
    }
    if(len==0){normalized[0]='/';normalized[1]=0;}
    struct stat st;if(stat(normalized,&st))return NULL;
    if(!resolved){resolved=malloc(strlen(normalized)+1);if(!resolved){errno=ENOMEM;return NULL;}}
    strcpy(resolved,normalized);return resolved;
}
DIR *opendir(const char *name){
    char *path=realpath(name,NULL);if(!path)return NULL;
    DIR *dir=calloc(1,sizeof *dir);if(!dir){free(path);errno=ENOMEM;return NULL;}
    FRESULT r=f_opendir(&dir->fat,path);free(path);
    if(r!=FR_OK){free(dir);ff_error(r);return NULL;}return dir;
}
struct dirent *readdir(DIR *dir){
    if(!dir){errno=EBADF;return NULL;}FILINFO info;FRESULT r=f_readdir(&dir->fat,&info);
    if(r!=FR_OK){ff_error(r);return NULL;}if(!info.fname[0])return NULL;
    size_t n=strlen(info.fname);if(n>=sizeof dir->entry.d_name){errno=ENAMETOOLONG;return NULL;}
    memcpy(dir->entry.d_name,info.fname,n+1);return &dir->entry;
}
int closedir(DIR *dir){if(!dir){errno=EBADF;return -1;}FRESULT r=f_closedir(&dir->fat);free(dir);return r==FR_OK?0:ff_error(r);}
void rewinddir(DIR *dir){if(dir)f_readdir(&dir->fat,NULL);}
int access(const char *name,int mode){struct stat st;if(stat(name,&st))return -1;if((mode&W_OK)&&!(st.st_mode&0222)){errno=EACCES;return -1;}return 0;}
int rmdir(const char *name){struct stat st;if(stat(name,&st))return -1;if(!S_ISDIR(st.st_mode)){errno=ENOTDIR;return -1;}return unlink(name);}
char *mkdtemp(char *pattern){
    size_t n=strlen(pattern);if(n<6||strcmp(pattern+n-6,"XXXXXX")){errno=EINVAL;return NULL;}
    for(unsigned tries=0;tries<32;tries++){
        unsigned value=(to_us_since_boot(get_absolute_time())+tries)&0xffffff;
        snprintf(pattern+n-6,7,"%06x",value);
        if(!mkdir(pattern,0700))return pattern;if(errno!=EEXIST)return NULL;
    }
    errno=EEXIST;return NULL;
}

/* SPDX-License-Identifier: BSD-3-Clause
 * Newlib stdio/FatFS adapter. Core0 owns all files. */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "pico/stdlib.h"
#include "hardware/psram.h"
#include "ff.h"
#include "omf_loop_diag.h"
#ifdef OMF_FILE_DIAG
#include <inttypes.h>
static struct {
    uint64_t requested,received,fat_requested,read_us,seek_us,forward_bytes,back_bytes;
    uint32_t sys_n,fat_n,short_n,eof_n,read_err,read_max,request_max,got_max;
    uint32_t req_bins[5],got_bins[5];
    uint32_t user_seek,seek_mode[4],seek_invalid,seek_n,seek_err,seek_max;
    uint32_t forward,back,same,clamped;
} file_diag;
static unsigned file_bin(uint32_t n){return !n?0:n<=64?1:n<=512?2:n<=4096?3:4;}
static FRESULT diag_read(FIL *f,void *buf,UINT request,UINT *got){
    uint32_t start=time_us_32();
    FRESULT result=f_read(f,buf,request,got);
    uint32_t elapsed=time_us_32()-start;
    file_diag.fat_n++;file_diag.fat_requested+=request;file_diag.received+=*got;
    file_diag.got_bins[file_bin(*got)]++;
    if(*got>file_diag.got_max)file_diag.got_max=*got;
    file_diag.read_us+=elapsed;if(elapsed>file_diag.read_max)file_diag.read_max=elapsed;
    if(result!=FR_OK)file_diag.read_err++;
    else {if(*got<request)file_diag.short_n++;if(request&&!*got)file_diag.eof_n++;}
    return result;
}
static FRESULT diag_seek(FIL *f,FSIZE_t target){
    FSIZE_t before=f_tell(f);
    uint32_t start=time_us_32();FRESULT result=f_lseek(f,target);
    uint32_t elapsed=time_us_32()-start;
    file_diag.seek_n++;file_diag.seek_us+=elapsed;
    if(elapsed>file_diag.seek_max)file_diag.seek_max=elapsed;
    if(target>before){file_diag.forward++;file_diag.forward_bytes+=(uint64_t)target-before;}
    else if(target<before){file_diag.back++;file_diag.back_bytes+=(uint64_t)before-target;}
    else file_diag.same++;
    if(result!=FR_OK)file_diag.seek_err++;
    else if(f_tell(f)!=target)file_diag.clamped++;
    return result;
}
void omf_file_diag_report(void){
    int saved_errno=errno;
    printf("OMF_FILE_READ sys=%lu req=%"PRIu64" fat=%lu fat_req=%"PRIu64" got=%"PRIu64" short=%lu eof=%lu err=%lu us=%"PRIu64" max_us=%lu req_max=%lu got_max=%lu\n",
      (unsigned long)file_diag.sys_n,file_diag.requested,(unsigned long)file_diag.fat_n,file_diag.fat_requested,file_diag.received,
      (unsigned long)file_diag.short_n,(unsigned long)file_diag.eof_n,(unsigned long)file_diag.read_err,file_diag.read_us,
      (unsigned long)file_diag.read_max,(unsigned long)file_diag.request_max,(unsigned long)file_diag.got_max);
    printf("OMF_FILE_SIZE req_bins=%lu/%lu/%lu/%lu/%lu got_bins=%lu/%lu/%lu/%lu/%lu\n",
      (unsigned long)file_diag.req_bins[0],(unsigned long)file_diag.req_bins[1],(unsigned long)file_diag.req_bins[2],(unsigned long)file_diag.req_bins[3],(unsigned long)file_diag.req_bins[4],
      (unsigned long)file_diag.got_bins[0],(unsigned long)file_diag.got_bins[1],(unsigned long)file_diag.got_bins[2],(unsigned long)file_diag.got_bins[3],(unsigned long)file_diag.got_bins[4]);
    printf("OMF_FILE_SEEK user=%lu modes=%lu/%lu/%lu/%lu invalid=%lu fat=%lu fwd=%lu back=%lu same=%lu fwd_bytes=%"PRIu64" back_bytes=%"PRIu64" clamp=%lu err=%lu us=%"PRIu64" max_us=%lu\n",
      (unsigned long)file_diag.user_seek,(unsigned long)file_diag.seek_mode[0],(unsigned long)file_diag.seek_mode[1],(unsigned long)file_diag.seek_mode[2],(unsigned long)file_diag.seek_mode[3],
      (unsigned long)file_diag.seek_invalid,(unsigned long)file_diag.seek_n,(unsigned long)file_diag.forward,(unsigned long)file_diag.back,(unsigned long)file_diag.same,
      file_diag.forward_bytes,file_diag.back_bytes,(unsigned long)file_diag.clamped,(unsigned long)file_diag.seek_err,file_diag.seek_us,(unsigned long)file_diag.seek_max);
    memset(&file_diag,0,sizeof file_diag);errno=saved_errno;
}

#define f_read diag_read
#define f_lseek diag_seek
#endif
static FIL *files[24];
static int file_flags[24];
#ifndef NATIVE_GAME_DIRECTORY
#define NATIVE_GAME_DIRECTORY "/roms/XU4"
#endif
static char cwd[256]=NATIVE_GAME_DIRECTORY;
static int fail(FRESULT r){
    errno=r==FR_NO_FILE||r==FR_NO_PATH?ENOENT:r==FR_EXIST?EEXIST:
        r==FR_WRITE_PROTECTED?EROFS:r==FR_DENIED?EACCES:r==FR_INVALID_NAME?EINVAL:EIO;
    return -1;
}
static int path_join(const char *name,char *out,size_t cap){
    if(!name||!*name){errno=ENOENT;return -1;}
    int n=snprintf(out,cap,name[0]=='/'?"%s":"%s/%s",name[0]=='/'?name:cwd,name);
    if(n<0||(size_t)n>=cap){errno=ENAMETOOLONG;return -1;}return 0;
}
int _open(const char *name,int flags,...){
    OMF_LOOP_SCOPE(OMF_LOOP_FILE);
    int slot;for(slot=3;slot<24&&files[slot];slot++);
    if(slot==24){errno=EMFILE;return -1;}
    char path[512];if(path_join(name,path,sizeof path))return -1;
    BYTE mode=(flags&O_ACCMODE)==O_RDONLY?FA_READ:(flags&O_ACCMODE)==O_WRONLY?FA_WRITE:FA_READ|FA_WRITE;
    if(flags&O_CREAT)mode|=(flags&O_EXCL)?FA_CREATE_NEW:(flags&O_TRUNC)?FA_CREATE_ALWAYS:FA_OPEN_ALWAYS;
    FIL *f=calloc(1,sizeof *f);if(!f){errno=ENOMEM;return -1;}
    FRESULT r=f_open(f,path,mode);if(r!=FR_OK){free(f);return fail(r);}
    if((flags&O_TRUNC)&&!(flags&O_CREAT)){r=f_truncate(f);}
    if(r==FR_OK&&(flags&O_APPEND))r=f_lseek(f,f_size(f));
    if(r!=FR_OK){f_close(f);free(f);return fail(r);}
    files[slot]=f;file_flags[slot]=flags;return slot;
}
int _close(int fd){
    OMF_LOOP_SCOPE(OMF_LOOP_FILE);
    if(fd<3||fd>=24||!files[fd]){errno=EBADF;return -1;}
    FRESULT r=f_close(files[fd]);free(files[fd]);files[fd]=NULL;return r==FR_OK?0:fail(r);
}
int _read(int fd,char *buf,int count){
    OMF_LOOP_SCOPE(OMF_LOOP_FILE);
    if(fd<3||fd>=24||!files[fd]||count<0){errno=EBADF;return -1;}
    UINT n=0;FRESULT r=FR_OK;
#ifdef OMF_FILE_DIAG
    file_diag.sys_n++;file_diag.requested+=(uint32_t)count;
    file_diag.req_bins[file_bin((uint32_t)count)]++;
    if((uint32_t)count>file_diag.request_max)file_diag.request_max=(uint32_t)count;
#endif
#ifdef OMF_READ_AUDIO
    extern void omf_native_audio_service(void);

    int saved_errno=errno;
    omf_native_audio_service();
    errno=saved_errno;
    do {
        UINT got=0, request=(UINT)(count-(int)n);
        if(request>4096u)request=4096u;
        r=f_read(files[fd],buf+n,request,&got);
        n+=got;
        saved_errno=errno;
        omf_native_audio_service();
        errno=saved_errno;
        if(r!=FR_OK||got<request)break;
    } while(n<(UINT)count);
#else
    r=f_read(files[fd],buf,count,&n);
#endif
#ifdef OMF_LOOP_DIAG
    omf_loop_read(n);
#endif
    return r==FR_OK?(int)n:fail(r);
}
int _write(int fd,char *buf,int count){
    if((fd==1||fd==2)&&count>=0){stdio_put_string(buf,count,false,true);return count;}
    OMF_LOOP_SCOPE(OMF_LOOP_FILE);
    if(fd<3||fd>=24||!files[fd]||count<0){errno=EBADF;return -1;}
    FRESULT r=FR_OK;
    if(file_flags[fd]&O_APPEND)r=f_lseek(files[fd],f_size(files[fd]));
    if(r!=FR_OK)return fail(r);
    UINT n=0;r=f_write(files[fd],buf,count,&n);return r==FR_OK?(int)n:fail(r);
}
off_t _lseek(int fd,off_t offset,int whence){
    OMF_LOOP_SCOPE(OMF_LOOP_FILE);
#ifdef OMF_FILE_DIAG
    file_diag.user_seek++;
    file_diag.seek_mode[whence==SEEK_SET?0:whence==SEEK_CUR?1:whence==SEEK_END?2:3]++;
    if(fd<3||fd>=24||!files[fd])file_diag.seek_invalid++;
#endif
    if(fd<3||fd>=24||!files[fd]){errno=EBADF;return -1;}
    int64_t base=whence==SEEK_SET?0:whence==SEEK_CUR?f_tell(files[fd]):whence==SEEK_END?f_size(files[fd]):-1;
    int64_t to=base+(int64_t)offset;
#ifdef OMF_FILE_DIAG
    if(base<0||to<0||to>INT32_MAX)file_diag.seek_invalid++;
#endif
    if(base<0||to<0||to>INT32_MAX){errno=EINVAL;return -1;}
    FRESULT r=f_lseek(files[fd],(FSIZE_t)to);return r==FR_OK?(off_t)f_tell(files[fd]):fail(r);
}
int _fstat(int fd,struct stat *s){
    memset(s,0,sizeof *s);
    if(fd>=0&&fd<3){s->st_mode=S_IFCHR;return 0;}
    if(fd<3||fd>=24||!files[fd]){errno=EBADF;return -1;}
    s->st_mode=S_IFREG|0666;s->st_size=f_size(files[fd]);return 0;
}
int _stat(const char *name,struct stat *s){
    OMF_LOOP_SCOPE(OMF_LOOP_FILE);
    char path[512];if(path_join(name,path,sizeof path))return -1;
    if(!strcmp(path,"/")){memset(s,0,sizeof *s);s->st_mode=S_IFDIR|0777;return 0;}
    FILINFO info;FRESULT r=f_stat(path,&info);if(r!=FR_OK)return fail(r);
    memset(s,0,sizeof *s);s->st_mode=(info.fattrib&AM_DIR?S_IFDIR:S_IFREG)|0777;s->st_size=info.fsize;return 0;
}
int mkdir(const char *name,mode_t mode){
    (void)mode;char path[512];if(path_join(name,path,sizeof path))return -1;
    FRESULT r=f_mkdir(path);return r==FR_OK?0:fail(r);
}
int chdir(const char *name){
    char path[256];if(path_join(name,path,sizeof path))return -1;
    FILINFO info;FRESULT r=f_stat(path,&info);if(r!=FR_OK)return fail(r);
    if(!(info.fattrib&AM_DIR)){errno=ENOTDIR;return -1;}strcpy(cwd,path);return 0;
}
char *getcwd(char *buffer,size_t size){
    size_t required=strlen(cwd)+1;
    if(size&&size<required){errno=ERANGE;return NULL;}
    if(!buffer){if(size==0)size=required;buffer=malloc(size);if(!buffer){errno=ENOMEM;return NULL;}}
    if(size<required){errno=ERANGE;return NULL;}
    memcpy(buffer,cwd,required);return buffer;
}
int _unlink(const char *name){char path[512];if(path_join(name,path,sizeof path))return -1;FRESULT r=f_unlink(path);return r==FR_OK?0:fail(r);}
int fsync(int fd){if(fd<3||fd>=24||!files[fd]){errno=EBADF;return -1;}FRESULT r=f_sync(files[fd]);return r==FR_OK?0:fail(r);}
int ftruncate(int fd,off_t size){
    if(fd<3||fd>=24||!files[fd]||size<0){errno=EINVAL;return -1;}
    FSIZE_t old=f_tell(files[fd]);FRESULT r=f_lseek(files[fd],size);
    if(r==FR_OK)r=f_truncate(files[fd]);
    if(r==FR_OK)r=f_lseek(files[fd],old);return r==FR_OK?0:fail(r);
}

extern char __psram_end__;
static uintptr_t heap_start,heap_break;
void *_sbrk(int amount){
    if(!psram_is_available()){errno=ENOMEM;return (void*)-1;}
    uintptr_t limit=0x11000000u+psram_get_size();
    if(!heap_start)heap_start=heap_break=((uintptr_t)&__psram_end__+7u)&~(uintptr_t)7u;
    if(heap_start<0x11000000u||heap_start>limit){errno=ENOMEM;return (void*)-1;}
    if((amount>0&&(uintptr_t)amount>limit-heap_break)||(amount<0&&(uint64_t)-(int64_t)amount>heap_break-heap_start)){
        errno=ENOMEM;return (void*)-1;
    }
    void *old=(void*)heap_break;heap_break+=amount;return old;
}

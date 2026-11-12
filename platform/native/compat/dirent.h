/* SPDX-License-Identifier: BSD-3-Clause */
#pragma once
typedef struct native_dir DIR;
struct dirent {char d_name[256];};
DIR *opendir(const char *path);
struct dirent *readdir(DIR *dir);
int readdir_r(DIR *dir,struct dirent *entry,struct dirent **result);
int closedir(DIR *dir);
void rewinddir(DIR *dir);

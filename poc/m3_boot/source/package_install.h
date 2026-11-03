#pragma once
#include <stddef.h>
#include <stdint.h>
/* Installs only missing files, never overwrites a save or an unknown resource.
 * Source is a mounted package tree. Manifest: mode<TAB>size<TAB>sha256<TAB>path.
 * The UI callback may cancel before a file is committed. */
typedef int (*PackageProgress)(const char *,uint64_t,void *);
int m3_install_tree(const char *source,const char *destination,PackageProgress progress,void *context,char error[512]);

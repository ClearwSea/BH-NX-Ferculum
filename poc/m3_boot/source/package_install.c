#if M3_STANDALONE || defined(PACKAGE_HOST_TEST)
#include "package_install.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef PACKAGE_HOST_TEST
#include <openssl/sha.h>
typedef SHA256_CTX Hash;
#define hash_init SHA256_Init
#define hash_add SHA256_Update
#define hash_end(c,b) SHA256_Final(b,c)
#else
#include <switch.h>
typedef Sha256Context Hash;
#define hash_init sha256ContextCreate
#define hash_add sha256ContextUpdate
#define hash_end sha256ContextGetHash
#endif
static int safe_path(const char *s){
 if(!*s || strlen(s)>350 || *s=='/')return 0;
 const char *part=s;
 for(const char *p=s;;p++){
  if(*p==':'||*p=='\\'||*p=='\t'||*p=='\r'||(*p && (unsigned char)*p<32))return 0;
  if(*p=='/'||!*p){size_t n=(size_t)(p-part);if(!n||(n==1&&part[0]=='.')||(n==2&&!memcmp(part,"..",2)))return 0;part=p+1;}
  if(!*p)break;
 }
 return 1;
}
static int mkdirs(const char *file){
 char b[512];if(strlen(file)>=sizeof b)return -1;strcpy(b,file);
 for(char *p=b+1;*p;p++)if(*p=='/' && p[-1]!=':'){
  *p=0;struct stat st;
  if(lstat(b,&st)==0){if(!S_ISDIR(st.st_mode))return -1;}
  else if(errno!=ENOENT||mkdir(b,0777))return -1;
  *p='/';
 }
 return 0;
}
static int digest(FILE *f,FILE *out,uint64_t *size,unsigned char hash[32],PackageProgress cb,const char *name,void *ctx){
 unsigned char *buf=malloc(256*1024);if(!buf)return -1;
 Hash h;hash_init(&h);*size=0;int ok=1;
 for(;;){size_t n=fread(buf,1,256*1024,f);if(!n){if(ferror(f))ok=0;break;}
  if(out&&fwrite(buf,1,n,out)!=n){ok=0;break;}
  hash_add(&h,buf,n);*size+=n;if(cb&&!cb(name,*size,ctx)){ok=0;break;}
 }
 hash_end(&h,hash);free(buf);return ok?0:-1;
}
int m3_install_tree(const char *source,const char *destination,PackageProgress cb,void *ctx,char error[512]){
 char manifest[512],line[512],src[512],dst[512],tmp[512];int result=-1;unsigned files=0;
 snprintf(manifest,sizeof manifest,"%s/manifest.tsv",source);FILE *m=fopen(manifest,"rb");
 if(!m){snprintf(error,512,"Cannot open package manifest");return -1;}
 while(fgets(line,sizeof line,m)){
  char mode,hex[65],name[351],extra;unsigned long long expected;
  size_t n=strlen(line);if(!n||line[n-1]!='\n')goto invalid;line[n-1]=0;
  if(sscanf(line,"%c\t%llu\t%64[0-9a-f]\t%350[^\n]%c",&mode,&expected,hex,name,&extra)!=4||strlen(hex)!=64||(mode!='R'&&mode!='S')||!safe_path(name))goto invalid;
  if(snprintf(src,sizeof src,"%s/payload/%s",source,name)>=(int)sizeof src||snprintf(dst,sizeof dst,"%s/%s",destination,name)>=(int)sizeof dst||snprintf(tmp,sizeof tmp,"%s.installpart",dst)>=(int)sizeof tmp)goto invalid;
  snprintf(error,512,"Preparing %.350s",name);
  if(mkdirs(dst))goto finish;
  struct stat st;int exists=lstat(dst,&st)==0;if(exists&&!S_ISREG(st.st_mode))goto finish;
  if(!exists&&errno!=ENOENT)goto finish;
  if(exists&&mode=='S'){files++;continue;}
  if(!exists&&!strcmp(name,"server-stage3/data/account.json")){
   char initialized[512];snprintf(initialized,sizeof initialized,"%s/server-stage3/data/initialized",destination);
   if(!access(initialized,F_OK)){snprintf(error,512,"Existing save is missing; restore it before starting.");goto finish;}
  }
  unsigned char want[32],got[32];for(unsigned i=0;i<32;i++){unsigned x;if(sscanf(hex+2*i,"%2x",&x)!=1)goto invalid;want[i]=(unsigned char)x;}
  FILE *in=fopen(exists?dst:src,"rb");if(!in)goto finish;
  FILE *out=NULL;
  if(!exists){struct stat ts;if(lstat(tmp,&ts)==0&&!S_ISREG(ts.st_mode)){fclose(in);goto finish;}out=fopen(tmp,"wb");if(!out){fclose(in);goto finish;}}
  uint64_t size=0;int rc=digest(in,out,&size,got,cb,name,ctx);if(fclose(in))rc=-1;
  if(out){if(fflush(out))rc=-1;if(fclose(out))rc=-1;}
  if(rc||size!=expected||memcmp(want,got,32)){snprintf(error,512,"File check failed: %.350s (existing files are preserved)",name);goto finish;}
  if(!exists&&rename(tmp,dst))goto finish;
  files++;
 }
 if(ferror(m)||!files)goto invalid;
 result=0;error[0]=0;goto finish;
invalid:snprintf(error,512,"Invalid package manifest; no further files installed.");
finish:fclose(m);return result;
}
#endif

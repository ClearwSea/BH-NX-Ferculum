/* Emulator-only boundary experiments. Mode 1 moves one call to a worker.
 * Mode 2 builds an independent shadow concurrently, compares serialized data,
 * and returns only the original result. Neither mode publishes early. */
#if M3_TABLE_WORKER_PROBE
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>
#include "bionic_pthread.h"
#include "dcr_sched.h"
#include "so_util.h"
#include "boot_profile.h"
#include "util.h"
#include "dcr_path.h"
typedef void *(*Invoke)(void *,void *,void **,void **);
static void *(*domain_get)(void);
static void *(*attach)(void *);
static Mutex lock;
static CondVar changed;
static int started,ready,pending,done,busy;
static b_pthread_t worker;
static struct {Invoke invoke;void *method,*instance,**args,**exception,*result;int error;uint64_t start,end;char name[64];} job;
#if M3_TABLE_WORKER_PROBE >= 2
static void *(*object_class)(void *),*(*object_new)(void *),*(*string_new)(const char *);
static void *(*get_method)(void *,const char *,int),*(*get_field)(void *,const char *);
static void (*field_get)(void *,void *,void *);
static void (*field_set)(void *,void *,void *);
static int (*is_value_type)(void *);
static void *(*unbox)(void *);
static void **(*assemblies)(void *,size_t *);
static void *(*image_get)(void *),*(*class_find)(void *,const char *,const char *);
static uint32_t (*root_new)(void *,int);
static void (*root_free)(uint32_t);
static int compare_ready;
static void *find_class(const char *name){
  size_t n=0;void **all=assemblies(domain_get(),&n);
  for(size_t i=0;i<n;i++){void *c=class_find(image_get(all[i]),"FullSerializer",name);if(c)return c;}
  return NULL;
}
static int value(void *object,const char *name,void *out){
  if(!object)return 0;void *f=get_field(object_class(object),name);
  if(!f)return 0;field_get(object,f,out);return 1;
}
static int success(void *result){unsigned char ok=0;return value(result,"_success",&ok)&&ok;}
static void *call(Invoke invoke,void *klass,const char *name,int argc,void *self,void **args,void **ex){
  void *m=get_method(klass,name,argc);if(!m)return NULL;
  return invoke(m,self,args,ex);
}
/* Match arm_typed_supply: a fresh serializer plus its configured processors.
 * Inputs are parsed independently; the fsData tree is never shared. */
static int prepare(Invoke invoke,void *original,const char *name,void **copy,void **parsed){
  if(!compare_ready)return 0;
  char path[512];snprintf(path,sizeof path,"%s/sdcard/bh3-server19-metadata/_exceloutputasset/%s",dcr_game_root(),name);
  FILE *f=fopen(path,"rb");if(!f)return 0;
  if(fseek(f,0,SEEK_END)){fclose(f);return 0;}long n=ftell(f);
  if(n<0||n>32*1024*1024||fseek(f,0,SEEK_SET)){fclose(f);return 0;}
  char *bytes=malloc((size_t)n+1);if(!bytes){fclose(f);return 0;}
  int ok=fread(bytes,1,(size_t)n,f)==(size_t)n;fclose(f);bytes[n]=0;
  void *text=ok?string_new(bytes):NULL;free(bytes);if(!text)return 0;
  uint32_t text_root=root_new(text,0);void *ex=NULL;
  void *parser=find_class("fsJsonParser");void *pa[2]={text,parsed};
  void *status=parser?call(invoke,parser,"Parse",2,NULL,pa,&ex):NULL;
  root_free(text_root);if(ex||!success(status)||!*parsed)return 0;
  void *klass=object_class(original);*copy=object_new(klass);if(!*copy)return 0;
  call(invoke,klass,".ctor",0,*copy,NULL,&ex);if(ex)return 0;
  void *processors=NULL,*items=NULL;uint32_t count=0;
  if(!value(original,"_processors",&processors)||!value(processors,"_items",&items)||!value(processors,"_size",&count)||!items)return 0;
  if(count>((uint32_t *)items)[3])return 0;
  for(uint32_t i=0;i<count;i++){
    void *a[1]={((void **)items)[4+i]};call(invoke,klass,"AddProcessor",1,*copy,a,&ex);if(ex)return 0;
  }
  return 1;
}
static void *encode(Invoke invoke,void *serializer,void *type,void *object){
  void *data=NULL,*ex=NULL;void *args[3]={type,object,&data};
  void *status=call(invoke,object_class(serializer),"TrySerialize",3,serializer,args,&ex);
  if(ex||!success(status)||!data)return NULL;
  void *printer=find_class("fsJsonPrinter"),*pa[1]={data};
  void *text=printer?call(invoke,printer,"CompressedJson",1,NULL,pa,&ex):NULL;
  return ex?NULL:text;
}
static void compare(Invoke invoke,void *serializer,void *copy,void *type,void *original,void *shadow,const char *name,int statuses,uint64_t main_start,uint64_t main_end){
  void *a=encode(invoke,serializer,type,original);uint32_t ar=a?root_new(a,0):0;
  void *b=encode(invoke,copy,type,shadow);
  uint32_t an=a?((uint32_t *)a)[2]:0,bn=b?((uint32_t *)b)[2]:0;
  int equal=statuses&&a&&b&&an==bn&&!memcmp((char *)a+12,(char *)b+12,(size_t)an*2);
  uint64_t lo=main_start>job.start?main_start:job.start,hi=main_end<job.end?main_end:job.end;
  unsigned char hash[32]={0};char hex[65];
  if(a)sha256CalculateHash(hash,(char *)a+12,(size_t)an*2);
  for(unsigned i=0;i<32;i++)sprintf(hex+2*i,"%02x",hash[i]);
  m3_boot_emit("[table-parallel-check] name=%s equal=%d statuses=%d chars=%u shadow_chars=%u overlap_us=%llu sha256=%s\n",name,equal,statuses,an,bn,(unsigned long long)(hi>lo?armTicksToNs(hi-lo)/1000:0),hex);
  if(ar)root_free(ar);
}
#endif
extern int m3_serializer_begin(void);
extern void m3_serializer_end(const char *);
static void *run(void *unused){
  (void)unused;
  void *thread=attach(domain_get());
  mutexLock(&lock);ready=thread?1:-1;condvarWakeAll(&changed);
  while(thread){
    while(!pending)condvarWait(&changed,&lock);
    pending=0;mutexUnlock(&lock);
    uint64_t start=armGetSystemTick();job.start=start;
    int scope=m3_serializer_begin();
    errno=job.error;
#if M3_TABLE_WORKER_PROBE >= 3
    extern void m3_serializer_cache_begin(void),m3_serializer_cache_end(void);
    m3_serializer_cache_begin();
#endif
    job.result=job.invoke(job.method,job.instance,job.args,job.exception);
#if M3_TABLE_WORKER_PROBE >= 3
    m3_serializer_cache_end();
#endif
    job.error=errno;
    job.end=armGetSystemTick();
    if(scope)m3_serializer_end(job.name);
    m3_boot_emit("[table-worker] name=%s thread=%u core=%u wall_us=%llu exception=%u result=%u\n",
      job.name,threadGetCurHandle(),(unsigned)svcGetCurrentProcessorNumber(),
      (unsigned long long)(armTicksToNs(armGetSystemTick()-start)/1000),
      job.exception&&*job.exception,job.result!=0);
    mutexLock(&lock);done=1;condvarWakeAll(&changed);
  }
  mutexUnlock(&lock);return NULL;
}
#if M3_TABLE_WORKER_PROBE >= 3
#include "table_split.inc"
#include "table_textmap.inc"
#endif
void m3_table_worker_install(so_module *mod){
  domain_get=(void *)so_try_find_addr_rx(mod,"il2cpp_domain_get");
  attach=(void *)so_try_find_addr_rx(mod,"il2cpp_thread_attach");
#if M3_TABLE_WORKER_PROBE >= 2
#define BIND(var,name) var=(void *)so_try_find_addr_rx(mod,"il2cpp_" name)
  BIND(object_class,"object_get_class");BIND(object_new,"object_new");BIND(string_new,"string_new");
  BIND(get_method,"class_get_method_from_name");BIND(get_field,"class_get_field_from_name");BIND(field_get,"field_get_value");
  BIND(assemblies,"domain_get_assemblies");BIND(image_get,"assembly_get_image");BIND(class_find,"class_from_name");
  BIND(field_set,"field_set_value");BIND(root_new,"gchandle_new");BIND(root_free,"gchandle_free");
  BIND(is_value_type,"class_is_valuetype");BIND(unbox,"object_unbox");
#undef BIND
  extern int m3_serializer_cache_ready(void);
  compare_ready=object_class&&object_new&&string_new&&get_method&&get_field&&field_get&&assemblies&&image_get&&class_find&&root_new&&root_free&&m3_serializer_cache_ready();
#endif
}
void *m3_table_worker_invoke(Invoke invoke,void *method,void *instance,void **args,void **exception,const char *name){
  int incoming=errno;
#if M3_TABLE_WORKER_PROBE >= 5
  return textmap_invoke(invoke,method,instance,args,exception,name);
#endif
#if M3_TABLE_WORKER_PROBE >= 3
  return split_invoke(invoke,method,instance,args,exception,name);
#endif
#if M3_TABLE_WORKER_PROBE == 2
  void *copy=NULL,*parsed=NULL,*shadow=NULL,*shadow_ex=NULL;
  if(!prepare(invoke,instance,name,&copy,&parsed)){
    m3_boot_emit("[table-parallel-check] name=%s prepare_failed=1\n",name);
    errno=incoming;return invoke(method,instance,args,exception);
  }
  uint32_t copy_root=root_new(copy,0),parsed_root=root_new(parsed,0);
  void *shadow_args[3]={parsed,args[1],&shadow};
#endif
  mutexLock(&lock);
  if(!started){
    started=1;
    if(!domain_get||!attach||b_pthread_create_on(&worker,NULL,run,NULL,DCR_GUEST_PRIO,2))ready=-1;
  }
  while(!ready)condvarWait(&changed,&lock);
  if(ready<0){mutexUnlock(&lock);debugPrintf("[table-worker] unavailable; original main-thread path\n");
#if M3_TABLE_WORKER_PROBE == 2
    root_free(copy_root);root_free(parsed_root);
#endif
    errno=incoming;return invoke(method,instance,args,exception);}
  while(busy)condvarWait(&changed,&lock);
  busy=1;
  job.invoke=invoke;job.method=method;job.instance=instance;job.args=args;job.exception=exception;job.error=incoming;
#if M3_TABLE_WORKER_PROBE == 2
  job.instance=copy;job.args=shadow_args;job.exception=&shadow_ex;
#endif
  strncpy(job.name,name,sizeof job.name-1);done=0;pending=1;condvarWakeAll(&changed);
#if M3_TABLE_WORKER_PROBE == 2
  mutexUnlock(&lock);uint64_t main_start=armGetSystemTick();errno=incoming;
  void *original=invoke(method,instance,args,exception);int original_errno=errno;
  uint64_t main_end=armGetSystemTick();mutexLock(&lock);
#endif
  while(!done)condvarWait(&changed,&lock);
  void *result=job.result;int saved=job.error;
#if M3_TABLE_WORKER_PROBE == 2
  mutexUnlock(&lock);
  compare(invoke,instance,copy,args[1],*(void **)args[2],shadow,name,!(exception&&*exception)&&!shadow_ex&&success(original)&&success(result),main_start,main_end);
  root_free(copy_root);root_free(parsed_root);result=original;saved=original_errno;
  mutexLock(&lock);
#endif
  job.method=job.instance=NULL;job.args=job.exception=NULL;job.result=NULL;
  busy=0;condvarWakeAll(&changed);
  mutexUnlock(&lock);errno=saved;return result;
}
#endif

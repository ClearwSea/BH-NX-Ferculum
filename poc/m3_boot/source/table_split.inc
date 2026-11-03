/* Two disjoint row ranges; original fsData remains intact for fallback/check. */
static void *clone_serializer(Invoke invoke,void *source,void **ex){
  void *k=object_class(source),*copy=object_new(k),*processors=NULL,*items=NULL;
  unsigned n=0;
  if(!copy)return NULL;
  call(invoke,k,".ctor",0,copy,NULL,ex);if(*ex)return NULL;
  if(!value(source,"_processors",&processors)||!value(processors,"_items",&items)||!value(processors,"_size",&n)||!items||n>((unsigned *)items)[3])return NULL;
  for(unsigned i=0;i<n;i++){void *a[]={((void **)items)[4+i]};call(invoke,k,"AddProcessor",1,copy,a,ex);if(*ex)return NULL;}
  return copy;
}
static void *range_data(Invoke invoke,void *data,void *rows,int first,int count,void **ex){
  void *a[]={&first,&count};
  void *range=call(invoke,object_class(rows),"GetRange",2,rows,a,ex);
  if(*ex||!range)return NULL;
  uint32_t rr=root_new(range,0);
  void *out=call(invoke,object_class(data),"CreateList",0,NULL,NULL,ex);
  if(out&&!*ex){void *f=get_field(object_class(out),"_value");if(f)field_set(out,f,range);else out=NULL;}
  root_free(rr);return out;
}
static int split_selected(const char *n){
  return !strcmp(n,"textmap")||!strcmp(n,"stagedata_main")||!strcmp(n,"randomdialogdata")||!strcmp(n,"rewarddata")||!strcmp(n,"stigmatadata")||!strcmp(n,"npclevellogic");
}
static void *split_invoke(Invoke invoke,void *method,void *instance,void **args,void **exception,const char *name){
  int incoming=errno;
  if(!compare_ready||!field_set||!split_selected(name))return invoke(method,instance,args,exception);
  uint64_t start=armGetSystemTick();void *ex=NULL,*rows=NULL,*left=NULL,*right=NULL,*copy=NULL,*lout=NULL,*rout=NULL,*lex=NULL,*rex=NULL,*ls=NULL,*rs=NULL;
  unsigned count=0;uint32_t roots[8]={0};unsigned nr=0;int valid=0;
  if(!value(args[0],"_value",&rows)||!value(rows,"_size",&count)||count<64)goto fallback;
  left=range_data(invoke,args[0],rows,0,count/2,&ex);if(ex||!left)goto fallback;roots[nr++]=root_new(left,0);
  right=range_data(invoke,args[0],rows,count/2,count-count/2,&ex);if(ex||!right)goto fallback;roots[nr++]=root_new(right,0);
  copy=clone_serializer(invoke,instance,&ex);if(ex||!copy)goto fallback;roots[nr++]=root_new(copy,0);
  mutexLock(&lock);
  if(!started){started=1;if(!domain_get||!attach||b_pthread_create_on(&worker,NULL,run,NULL,DCR_GUEST_PRIO,2))ready=-1;}
  while(!ready)condvarWait(&changed,&lock);
  if(ready<0||busy){mutexUnlock(&lock);goto fallback;}
  busy=1;void *ra[]={right,args[1],&rout},*la[]={left,args[1],&lout};
  job.invoke=invoke;job.method=method;job.instance=copy;job.args=ra;job.exception=&rex;job.error=incoming;
  strncpy(job.name,name,sizeof job.name-1);done=0;pending=1;condvarWakeAll(&changed);mutexUnlock(&lock);
  extern void m3_serializer_cache_begin(void),m3_serializer_cache_end(void);
  m3_serializer_cache_begin();
  uint64_t ms=armGetSystemTick();ls=invoke(method,instance,la,&lex);uint64_t me=armGetSystemTick();
  m3_serializer_cache_end();
  if(ls)roots[nr++]=root_new(ls,0);if(lout)roots[nr++]=root_new(lout,0);
  mutexLock(&lock);while(!done)condvarWait(&changed,&lock);rs=job.result;mutexUnlock(&lock);
  if(rout)roots[nr++]=root_new(rout,0);
  if(!lex&&!rex&&lout&&rout&&success(ls)&&success(rs)){
    void *a[]={rout};call(invoke,object_class(lout),"AddRange",1,lout,a,&ex);
    unsigned merged=0;valid=!ex&&value(lout,"_size",&merged)&&merged==count;
  }
  uint64_t lo=ms>job.start?ms:job.start,hi=me<job.end?me:job.end;
  m3_boot_emit("[table-split] name=%s rows=%u valid=%u wall_us=%llu overlap_us=%llu\n",name,count,valid,(unsigned long long)(armTicksToNs(armGetSystemTick()-start)/1000),(unsigned long long)(hi>lo?armTicksToNs(hi-lo)/1000:0));
  mutexLock(&lock);busy=0;job.method=job.instance=job.result=NULL;job.args=job.exception=NULL;condvarWakeAll(&changed);mutexUnlock(&lock);
#if M3_TABLE_WORKER_PROBE == 3
  if(valid){
    void *original=invoke(method,instance,args,exception);
    compare(invoke,instance,copy,args[1],*(void **)args[2],lout,name,!(exception&&*exception)&&success(original),ms,me);
    while(nr)root_free(roots[--nr]);errno=incoming;return original;
  }
#endif
  if(valid){*(void **)args[2]=lout;if(exception)*exception=NULL;while(nr)root_free(roots[--nr]);errno=incoming;return ls;}
fallback:
  while(nr)root_free(roots[--nr]);
  m3_boot_emit("[table-split] name=%s fallback=1\n",name);errno=incoming;
  return invoke(method,instance,args,exception);
}

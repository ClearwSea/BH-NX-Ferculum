/* Pinned TextMapMetaData schema: exactly ID/Text strings, no nested objects.
 * Build the same managed List through its API; validate mode 5 against the
 * complete original serializer. Mode 6 uses the result, with serial fallback. */
static void *textmap_invoke(Invoke invoke,void *method,void *instance,void **args,void **exception,const char *name){
  if(strcmp(name,"textmap")||!compare_ready||!is_value_type||!unbox)return invoke(method,instance,args,exception);
  void *rows=NULL,*items=NULL,*ex=NULL,*data=NULL,*out=NULL,*status=NULL,*klass=NULL;
  uint32_t roots[8]={0};unsigned nr=0,n=0;uint64_t start=armGetSystemTick();
  if(is_value_type(object_class(args[0]))||!value(args[0],"_value",&rows)||!value(rows,"_items",&items)||!value(rows,"_size",&n)||!items||n>((unsigned *)items)[3])goto fallback_text;
  size_t ac=0;void **all=assemblies(domain_get(),&ac);
  for(size_t i=0;i<ac&&!klass;i++)klass=class_find(image_get(all[i]),"MoleMole","TextMapMetaData");
  if(!klass)goto fallback_text;
  void *idfield=get_field(klass,"ID"),*textfield=get_field(klass,"Text");
  if(!idfield||!textfield)goto fallback_text;
  data=range_data(invoke,args[0],rows,0,0,&ex);if(ex||!data)goto fallback_text;roots[nr++]=root_new(data,0);
  void *da[]={data,args[1],&out};status=invoke(method,instance,da,&ex);
  if(ex||!success(status)||!out)goto fallback_text;roots[nr++]=root_new(out,0);roots[nr++]=root_new(status,0);
  void *add=get_method(object_class(out),"Add",1),*keyid=string_new("ID"),*keytext=string_new("Text");
  roots[nr++]=root_new(keyid,0);roots[nr++]=root_new(keytext,0);
  void *stringclass=object_class(keyid);int boxed=is_value_type(klass);
  if(!add)goto fallback_text;
  for(unsigned i=0;i<n;i++){
    void *row=((void **)items)[4+i],*dict=NULL,*id=NULL,*text=NULL;
    if(!value(row,"_value",&dict)||!dict)goto fallback_text;
    void *key[]={keyid};void *iv=call(invoke,object_class(dict),"get_Item",1,dict,key,&ex);
    key[0]=keytext;void *tv=call(invoke,object_class(dict),"get_Item",1,dict,key,&ex);
    if(ex||!value(iv,"_value",&id)||!value(tv,"_value",&text)||(id&&object_class(id)!=stringclass)||(text&&object_class(text)!=stringclass))goto fallback_text;
    void *obj=object_new(klass);if(!obj)goto fallback_text;
    field_set(obj,idfield,id);field_set(obj,textfield,text);
    void *a[]={boxed?unbox(obj):obj};invoke(add,out,a,&ex);if(ex)goto fallback_text;
  }
  m3_boot_emit("[table-textmap] rows=%u wall_us=%llu valid=1\n",n,(unsigned long long)(armTicksToNs(armGetSystemTick()-start)/1000));
#if M3_TABLE_WORKER_PROBE == 5
  {
    void *original=invoke(method,instance,args,exception);
    compare(invoke,instance,instance,args[1],*(void **)args[2],out,name,!(exception&&*exception)&&success(original),0,0);
    while(nr)root_free(roots[--nr]);return original;
  }
#endif
  *(void **)args[2]=out;if(exception)*exception=NULL;while(nr)root_free(roots[--nr]);return status;
fallback_text:
  while(nr)root_free(roots[--nr]);m3_boot_emit("[table-textmap] fallback=1\n");
  return invoke(method,instance,args,exception);
}

#include "boot_profile.h"
/* In-process byte streams and Python host. Never initializes BSD sockets. */
#include "local_server.h"
#include "port_config.h"
#define M3_WIDE_INNER(x) L##x
#define M3_WIDE(x) M3_WIDE_INNER(x)
#ifndef M3_EMBEDDED_SERVER
#define M3_EMBEDDED_SERVER 0
#endif
#if M3_EMBEDDED_SERVER
#include <Python.h>
#include <switch.h>
#include <sys/iosupport.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "numpy_inittab.h"
#include "util.h"

#define CAP (256 * 1024)
typedef struct { unsigned char *bytes; size_t head, size; } Pipe;
typedef struct Channel {
  Mutex lock; CondVar changed;
  Pipe input, output;
  unsigned refs;
  int client_closed, server_closed, write_closed, read_closed;
  uint64_t server_timeout;
  uint64_t client_tx, server_rx, server_tx, client_rx;
  uint32_t server_hash, client_hash;
} Channel;
typedef struct { Channel *channel; int nonblock; uint64_t recv_ns, send_ns; } LocalFD;
typedef struct Job {
  struct Job *next; int kind, done; unsigned refs;
  Channel *channel;
  char *request, *response; size_t request_size, response_size;
} Job;
static Mutex gate;
static CondVar changed;
static Job *jobs, *tail;
static int state; /* 0 absent, 1 initializing, 2 ready, -1 failed */
static Thread worker;
extern void __libnx_init_time(void);
static LocalFD *fds[1024];
static uint64_t now_ns(void) { return armTicksToNs(armGetSystemTick()); }
static void stream_hash(uint32_t *hash,const void *data,size_t size){
  const unsigned char *p=data;
  while(size--)*hash=(*hash ^ *p++)*16777619U;
}
static void release_channel(Channel *c) {
  if (__atomic_sub_fetch(&c->refs, 1, __ATOMIC_ACQ_REL) == 0) {
    free(c->input.bytes); free(c->output.bytes); free(c);
  }
}
static void release_job(Job *j) {
  if (__atomic_sub_fetch(&j->refs, 1, __ATOMIC_ACQ_REL) == 0) {
    free(j->request); free(j->response); free(j);
  }
}
static size_t put(Pipe *p, const void *src, size_t n) {
  if (n > CAP - p->size) n = CAP - p->size;
  size_t at = (p->head + p->size) % CAP, first = CAP - at;
  if (first > n) first = n;
  memcpy(p->bytes + at, src, first); memcpy(p->bytes, (const char *)src + first, n - first);
  p->size += n; return n;
}
static size_t take(Pipe *p, void *dst, size_t n, int peek) {
  if (n > p->size) n = p->size;
  size_t first = CAP - p->head; if (first > n) first = n;
  memcpy(dst, p->bytes + p->head, first); memcpy((char *)dst + first, p->bytes, n - first);
  if (!peek) { p->head = (p->head + n) % CAP; p->size -= n; }
  return n;
}
static void enqueue(Job *j) {
  if (tail) tail->next = j; else jobs = j;
  tail = j; condvarWakeAll(&changed);
}
static int wait_channel(Channel *c, uint64_t deadline) {
  if (!deadline) { condvarWait(&c->changed, &c->lock); return 1; }
  uint64_t now = now_ns(); if (now >= deadline) return 0;
  condvarWaitTimeout(&c->changed, &c->lock, deadline - now);
  return now_ns() < deadline;
}
static LocalFD *descriptor(int fd) {
  if (fd < 0 || fd >= 1024 || !fds[fd]) { errno = EBADF; return NULL; }
  return fds[fd];
}
static int snapshot(int fd, LocalFD *out) {
  mutexLock(&gate); LocalFD *f=descriptor(fd);
  if(f){*out=*f;if(out->channel)__atomic_add_fetch(&out->channel->refs,1,__ATOMIC_RELAXED);}
  mutexUnlock(&gate);return f!=NULL;
}
static ssize_t finish_io(Channel *c,ssize_t result,int error) {
  mutexUnlock(&c->lock);release_channel(c);if(error)errno=error;return result;
}
static int dev_open(struct _reent *r, void *storage, const char *path, int flags, int mode) {
  (void)r; (void)path; (void)mode;
  LocalFD *f = storage; memset(f, 0, sizeof *f); f->nonblock = !!(flags & O_NONBLOCK); return 0;
}
static int dev_close(struct _reent *r, void *storage) {
  (void)r; LocalFD *f = storage; Channel *c = f->channel;
  if (c) { mutexLock(&c->lock); c->client_closed = 1; condvarWakeAll(&c->changed); mutexUnlock(&c->lock); release_channel(c); }
  return 0;
}
static const devoptab_t device = {.name="m3ipc", .structSize=sizeof(LocalFD), .open_r=dev_open, .close_r=dev_close};
int m3_local_socket(int nonblock) {
  int fd = open("m3ipc:", O_RDWR | (nonblock ? O_NONBLOCK : 0));
  if (fd < 0) return -1;
  if (fd >= 1024) { close(fd); errno = EMFILE; return -1; }
  mutexLock(&gate);fds[fd] = __get_handle(fd)->fileStruct;mutexUnlock(&gate);
  return fd;
}
int m3_local_connect(int fd) {
  mutexLock(&gate);
  LocalFD *f = descriptor(fd); if (!f) {mutexUnlock(&gate);return -1;}
  if (f->channel) { mutexUnlock(&gate);errno = EISCONN; return -1; }
  Channel *c = calloc(1, sizeof *c); Job *j = calloc(1, sizeof *j);
  if (!c || !j) { free(c); free(j);mutexUnlock(&gate); errno = ENOMEM; return -1; }
  c->input.bytes = malloc(CAP); c->output.bytes = malloc(CAP);
  if (!c->input.bytes || !c->output.bytes) { free(c->input.bytes); free(c->output.bytes); free(c); free(j);mutexUnlock(&gate); errno=ENOMEM; return -1; }
  c->refs=2; c->server_timeout=120000000000ULL;
  c->server_hash=c->client_hash=2166136261U; j->kind=1; j->channel=c;
  if (state != 2) { mutexUnlock(&gate); release_channel(c); release_channel(c); free(j); errno=ENETDOWN; return -1; }
  f->channel=c; enqueue(j); mutexUnlock(&gate); return 0;
}
int m3_local_close(int fd) { mutexLock(&gate);if (!descriptor(fd)){mutexUnlock(&gate);return -1;} fds[fd]=NULL;mutexUnlock(&gate); return close(fd); }
int m3_local_fcntl(int fd, int cmd, long arg) {
  mutexLock(&gate);LocalFD *f=descriptor(fd);int result=-1;
  if(f){
    if(cmd==3)result=f->nonblock ? 0x800:0;
    else if(cmd==4){f->nonblock=!!(arg&0x800);result=0;}
    else if(cmd==1 || cmd==2)result=0;
    else errno=EINVAL;
  }
  mutexUnlock(&gate);return result;
}
int m3_local_ioctl(int fd, unsigned long req, void *arg) {
  if(!arg){errno=EINVAL;return -1;}
  if(req==0x5421)return m3_local_fcntl(fd,4,*(int *)arg ? 0x800:0);
  LocalFD f;if(!snapshot(fd,&f))return -1;
  int result=-1;
  if(req==0x541b){
    *(int *)arg=0;
    if(f.channel){mutexLock(&f.channel->lock);*(int *)arg=f.channel->output.size;mutexUnlock(&f.channel->lock);}
    result=0;
  }else errno=EINVAL;
  if(f.channel)release_channel(f.channel);return result;
}
short m3_local_ready(int fd, short events) {
  LocalFD f;if(!snapshot(fd,&f))return 32;
  Channel *c=f.channel; if (!c) return 0;
  mutexLock(&c->lock); short ready=0;
  if ((events&1) && (c->output.size || c->server_closed || c->read_closed)) ready|=1;
  if ((events&4) && !c->server_closed && !c->write_closed && c->input.size<CAP) ready|=4;
  if (c->server_closed) ready|=16;
  mutexUnlock(&c->lock);release_channel(c); return ready;
}
ssize_t m3_local_send(int fd, const void *buf, size_t n, int flags) {
  LocalFD f;if(!snapshot(fd,&f))return -1;
  Channel *c=f.channel;if(!c){errno=ENOTCONN;return -1;} if(!n){release_channel(c);return 0;}
  uint64_t deadline=f.send_ns ? now_ns()+f.send_ns : 0;
  mutexLock(&c->lock);
  while(c->input.size==CAP && !c->server_closed && !c->write_closed && !c->client_closed){
    if(f.nonblock || (flags&0x40))return finish_io(c,-1,EAGAIN);
    if(!wait_channel(c,deadline))return finish_io(c,-1,EAGAIN);
  }
  if(c->server_closed || c->write_closed || c->client_closed)return finish_io(c,-1,EPIPE);
  n=put(&c->input,buf,n);c->client_tx+=n;condvarWakeAll(&c->changed);return finish_io(c,n,0);
}
ssize_t m3_local_recv(int fd, void *buf, size_t n, int flags) {
  LocalFD f;if(!snapshot(fd,&f))return -1;
  Channel *c=f.channel;if(!c){errno=ENOTCONN;return -1;}if(!n){release_channel(c);return 0;}
  uint64_t deadline=f.recv_ns ? now_ns()+f.recv_ns : 0;
  mutexLock(&c->lock);
  while(!c->output.size && !c->server_closed && !c->read_closed && !c->client_closed){
    if(f.nonblock || (flags&0x40))return finish_io(c,-1,EAGAIN);
    if(!wait_channel(c,deadline))return finish_io(c,-1,EAGAIN);
  }
  n=c->read_closed ? 0 : take(&c->output,buf,n,flags&2);
  if(!(flags&2)){c->client_rx+=n;stream_hash(&c->client_hash,buf,n);}
  condvarWakeAll(&c->changed);return finish_io(c,n,0);
}
int m3_local_getsockopt(int fd,int option,void *value,unsigned *len){
  LocalFD f;if(!snapshot(fd,&f))return -1;if(f.channel)release_channel(f.channel);
  if(!value || !len || *len<4 || (option!=4 && option!=3)){errno=EINVAL;return -1;}
  *(int *)value=option==4 ? 0 : 1;*len=4;return 0;
}
int m3_local_setsockopt(int fd,int level,int option,const void *value,unsigned len){
  mutexLock(&gate);LocalFD *f=descriptor(fd);int result=-1;
  if(f){
    if(level==1 && (option==20 || option==21)){
      int32_t t[2];
      if(!value || len!=sizeof t)errno=EINVAL;
      else{memcpy(t,value,sizeof t);
        if(t[0]<0 || t[1]<0 || t[1]>=1000000)errno=EINVAL;
        else{uint64_t ns=(uint64_t)t[0]*1000000000ULL+(uint64_t)t[1]*1000;
          if(option==20)f->recv_ns=ns;else f->send_ns=ns;result=0;}
      }
    }else if((level==6 && option==1)||(level==1 && (option==2 || option==7 || option==8 || option==9)))result=0;
    else errno=EOPNOTSUPP;
  }
  mutexUnlock(&gate);return result;
}
int m3_local_shutdown(int fd,int how){
  if(how<0 || how>2){errno=EINVAL;return -1;}
  LocalFD f;if(!snapshot(fd,&f))return -1;
  Channel *c=f.channel;if(!c){errno=ENOTCONN;return -1;}
  mutexLock(&c->lock);if(how!=1)c->read_closed=1;if(how!=0)c->write_closed=1;
  condvarWakeAll(&c->changed);mutexUnlock(&c->lock);release_channel(c);return 0;
}
static void channel_capsule_free(PyObject *cap){
  Channel *c=PyCapsule_GetPointer(cap,"nx.channel");if(!c){PyErr_Clear();return;}
  mutexLock(&c->lock);c->server_closed=1;condvarWakeAll(&c->changed);mutexUnlock(&c->lock);release_channel(c);
}
static void job_capsule_free(PyObject *cap){Job *j=PyCapsule_GetPointer(cap,"nx.http");if(j)release_job(j);else PyErr_Clear();}
static PyObject *py_ready(PyObject *self,PyObject *args){
  (void)self;int files;if(!PyArg_ParseTuple(args,"i",&files))return NULL;
  mutexLock(&gate);state=2;condvarWakeAll(&changed);mutexUnlock(&gate);
  debugPrintf("[local] ready verified_files=%d no BSD listeners\n",files);Py_RETURN_NONE;
}
static PyObject *py_schedule_thread(PyObject *self,PyObject *args){
  (void)self;(void)args;
  s32 before=0,core=0;u64 mask=0;
  Result rc=svcGetThreadPriority(&before,CUR_THREAD_HANDLE);
  if(R_SUCCEEDED(rc))rc=svcGetThreadCoreMask(&core,&mask,CUR_THREAD_HANDLE);
  if(R_SUCCEEDED(rc))rc=svcSetThreadPriority(CUR_THREAD_HANDLE,44);
  if(R_FAILED(rc)){
    PyErr_Format(PyExc_RuntimeError,"NX server thread scheduling failed: %x",rc);
    return NULL;
  }
  debugPrintf("[local] session scheduling priority=%d->44 core=%d mask=%llx (preserved)\n",
              before,core,(unsigned long long)mask);
  return Py_BuildValue("iiK",before,core,(unsigned long long)mask);
}
static PyObject *py_channel_stats(PyObject *self,PyObject *args){
  (void)self;(void)args;PyObject *rows=PyList_New(0);if(!rows)return NULL;
  for(int fd=0;fd<1024;fd++){
    mutexLock(&gate);Channel *c=fds[fd] ? fds[fd]->channel:NULL;
    if(c)__atomic_add_fetch(&c->refs,1,__ATOMIC_RELAXED);mutexUnlock(&gate);
    if(!c)continue;
    mutexLock(&c->lock);
    uint64_t tx=c->client_tx,srx=c->server_rx,stx=c->server_tx,rx=c->client_rx;
    unsigned in=c->input.size,out=c->output.size,sh=c->server_hash,ch=c->client_hash;
    mutexUnlock(&c->lock);release_channel(c);
    PyObject *row=Py_BuildValue("{s:i,s:K,s:K,s:K,s:K,s:I,s:I,s:I,s:I}",
      "fd",fd,"client_tx",tx,"server_rx",srx,"server_tx",stx,"client_rx",rx,
      "input_waiting",in,"output_waiting",out,"server_hash",sh,"client_hash",ch);
    if(!row || PyList_Append(rows,row)<0){Py_XDECREF(row);Py_DECREF(rows);return NULL;}
    Py_DECREF(row);
  }
  return rows;
}
static PyObject *py_next(PyObject *self,PyObject *args){
  (void)self;(void)args;Job *j;
  Py_BEGIN_ALLOW_THREADS
  mutexLock(&gate);while(!jobs)condvarWait(&changed,&gate);
  j=jobs;jobs=j->next;if(!jobs)tail=NULL;mutexUnlock(&gate);
  Py_END_ALLOW_THREADS
  if(j->kind==1){PyObject *cap=PyCapsule_New(j->channel,"nx.channel",channel_capsule_free);free(j);if(!cap)return NULL;return Py_BuildValue("sNO","connect",cap,Py_None);}
  PyObject *cap=PyCapsule_New(j,"nx.http",job_capsule_free);if(!cap)return NULL;
  PyObject *body=PyBytes_FromStringAndSize(j->request,j->request_size);if(!body){Py_DECREF(cap);return NULL;}
  return Py_BuildValue("sNN","http",cap,body);
}
static PyObject *py_timeout(PyObject *self,PyObject *args){
  (void)self;PyObject *cap;double seconds;if(!PyArg_ParseTuple(args,"Od",&cap,&seconds))return NULL;
  Channel *c=PyCapsule_GetPointer(cap,"nx.channel");if(!c)return NULL;
  mutexLock(&c->lock);c->server_timeout=seconds>0 ? (uint64_t)(seconds*1000000000.) : 0;mutexUnlock(&c->lock);Py_RETURN_NONE;
}
static PyObject *py_recv(PyObject *self,PyObject *args){
  (void)self;PyObject *cap;Py_ssize_t requested;if(!PyArg_ParseTuple(args,"On",&cap,&requested))return NULL;
  Channel *c=PyCapsule_GetPointer(cap,"nx.channel");if(!c)return NULL;
  if(requested<0){PyErr_SetString(PyExc_ValueError,"negative recv size");return NULL;}
  if(requested>CAP)requested=CAP;
  char *bytes=malloc(requested ? requested : 1);if(!bytes)return PyErr_NoMemory();size_t n=0;int timed_out=0;
  Py_BEGIN_ALLOW_THREADS
  mutexLock(&c->lock);uint64_t deadline=c->server_timeout ? now_ns()+c->server_timeout : 0;
  while(requested && !c->input.size && !c->client_closed && !c->write_closed && !c->server_closed){if(!wait_channel(c,deadline)){timed_out=1;break;}}
  if(!timed_out){n=take(&c->input,bytes,requested,0);c->server_rx+=n;}
  condvarWakeAll(&c->changed);mutexUnlock(&c->lock);
  Py_END_ALLOW_THREADS
  if(timed_out){free(bytes);PyErr_SetString(PyExc_TimeoutError,"NX channel timeout");return NULL;}
  PyObject *result=PyBytes_FromStringAndSize(bytes,n);free(bytes);return result;
}
static PyObject *py_sendall(PyObject *self,PyObject *args){
  (void)self;PyObject *cap;Py_buffer b;if(!PyArg_ParseTuple(args,"Oy*",&cap,&b))return NULL;
  Channel *c=PyCapsule_GetPointer(cap,"nx.channel");if(!c){PyBuffer_Release(&b);return NULL;}
  size_t sent=0;int broken=0;
  Py_BEGIN_ALLOW_THREADS
  mutexLock(&c->lock);
  while(sent<(size_t)b.len){
    while(c->output.size==CAP && !c->client_closed && !c->read_closed)condvarWait(&c->changed,&c->lock);
    if(c->client_closed || c->read_closed){broken=1;break;}
    size_t n=put(&c->output,(char *)b.buf+sent,b.len-sent);
    stream_hash(&c->server_hash,(char *)b.buf+sent,n);c->server_tx+=n;
    sent+=n;condvarWakeAll(&c->changed);
  }
  mutexUnlock(&c->lock);
  Py_END_ALLOW_THREADS
  PyBuffer_Release(&b);if(broken){PyErr_SetString(PyExc_BrokenPipeError,"NX channel closed");return NULL;}Py_RETURN_NONE;
}
static PyObject *py_server_close(PyObject *self,PyObject *args){
  (void)self;PyObject *cap;if(!PyArg_ParseTuple(args,"O",&cap))return NULL;
  Channel *c=PyCapsule_GetPointer(cap,"nx.channel");if(!c)return NULL;
  mutexLock(&c->lock);c->server_closed=1;condvarWakeAll(&c->changed);mutexUnlock(&c->lock);Py_RETURN_NONE;
}
static PyObject *py_http_done(PyObject *self,PyObject *args){
  (void)self;PyObject *cap;Py_buffer b;if(!PyArg_ParseTuple(args,"Oy*",&cap,&b))return NULL;
  Job *j=PyCapsule_GetPointer(cap,"nx.http");if(!j){PyBuffer_Release(&b);return NULL;}
  char *response=malloc(b.len+1);if(!response){PyBuffer_Release(&b);return PyErr_NoMemory();}
  memcpy(response,b.buf,b.len);response[b.len]=0;
  mutexLock(&gate);j->response=response;j->response_size=b.len;j->done=1;condvarWakeAll(&changed);mutexUnlock(&gate);
  PyBuffer_Release(&b);Py_RETURN_NONE;
}
static PyMethodDef methods[]={
  {"channel_stats",py_channel_stats,METH_NOARGS,NULL},
  {"schedule_thread",py_schedule_thread,METH_NOARGS,NULL},
  {"ready",py_ready,METH_VARARGS,NULL},{"next_job",py_next,METH_NOARGS,NULL},
  {"settimeout",py_timeout,METH_VARARGS,NULL},{"recv",py_recv,METH_VARARGS,NULL},
  {"sendall",py_sendall,METH_VARARGS,NULL},{"server_close",py_server_close,METH_VARARGS,NULL},
  {"http_done",py_http_done,METH_VARARGS,NULL},{NULL,NULL,0,NULL}};
static struct PyModuleDef module={PyModuleDef_HEAD_INIT,"_nxipc",NULL,-1,methods,NULL,NULL,NULL,NULL};
static PyObject *PyInit__nxipc(void){return PyModule_Create(&module);}
static void serve(void *unused){
  (void)unused;int ok=register_numpy()==0 && PyImport_AppendInittab("_nxipc",PyInit__nxipc)==0;
  m3_boot_mark("python-init-begin");
  PyConfig config;PyConfig_InitIsolatedConfig(&config);config.site_import=0;config.write_bytecode=0;config.install_signal_handlers=0;config.module_search_paths_set=1;
  PyStatus status=PyWideStringList_Append(&config.module_search_paths,M3_WIDE(M3_PYTHON_ROOT "/stdlib"));
  if(!PyStatus_Exception(status))status=PyWideStringList_Append(&config.module_search_paths,M3_WIDE(M3_PYTHON_ROOT));
  if(!PyStatus_Exception(status))status=PyWideStringList_Append(&config.module_search_paths,M3_WIDE(M3_SERVER_ROOT));
  if(!PyStatus_Exception(status))status=PyConfig_SetString(&config,&config.filesystem_encoding,L"utf-8");
  if(ok && !PyStatus_Exception(status))status=Py_InitializeFromConfig(&config);
  PyConfig_Clear(&config);
  m3_boot_mark("python-init-end");
  if(!ok || PyStatus_Exception(status)){debugPrintf("[local] Python init failed: %s\n",ok ? status.err_msg : "builtins");}
  else{
    int result=PyRun_SimpleString(
      "import sys\n"
      "sys.stdout=open('" M3_SERVER_ROOT "/server-console.log','a',buffering=1)\n"
      "sys.stderr=sys.stdout\n"
      "import _nxipc,nx_boot_profile\n"
      "nx_boot_profile.PYTHON_ROOT='" M3_PYTHON_ROOT "'\n"
      "nx_boot_profile.run(_nxipc)\n"
);
    debugPrintf("[local] server worker exited result=%d\n",result);
  }
  mutexLock(&gate);state=-1;condvarWakeAll(&changed);mutexUnlock(&gate);
  /* The game owns process lifetime. Never finalize Python while a session thread runs. */
}
int m3_local_start(void){
  mutexLock(&gate);
  if(!state){
    state=1;
    /* The custom game __appInit opens time services but omits this newlib
       epoch setup. Python needs calendar time for daily/weekly server state. */
    __libnx_init_time();
    if(AddDevice(&device)<0){state=-1;}
    else{Result rc=threadCreate(&worker,serve,NULL,NULL,1024*1024,44,-2);if(R_SUCCEEDED(rc))rc=threadStart(&worker);if(R_FAILED(rc)){state=-1;debugPrintf("[local] worker start failed rc=%x\n",rc);}}
  }
  uint64_t deadline=now_ns()+60000000000ULL;
  while(state==1){uint64_t now=now_ns();if(now>=deadline)break;condvarWaitTimeout(&changed,&gate,deadline-now);}
  int ready=state==2;mutexUnlock(&gate);return ready;
}
int m3_local_http(const char *path,const void *post,size_t post_len,char **raw,size_t *size){
  Job *j=calloc(1,sizeof *j);if(!j)return 0;
  size_t capacity=strlen(path)+post_len+512;j->request=malloc(capacity);
  if(!j->request){free(j);return 0;}
  int header=snprintf(j->request,capacity,"%s %s HTTP/1.0\r\nHost: 127.0.0.1:21118\r\nContent-Length: %lu\r\nContent-Type: application/x-www-form-urlencoded\r\nConnection: close\r\n\r\n",post ? "POST":"GET",path,(unsigned long)post_len);
  if(header<0 || (size_t)header+post_len>=capacity){free(j->request);free(j);return 0;}
  if(post_len)memcpy(j->request+header,post,post_len);j->request_size=header+post_len;j->kind=2;j->refs=2;
  mutexLock(&gate);if(state!=2){mutexUnlock(&gate);release_job(j);release_job(j);return 0;}
  enqueue(j);uint64_t deadline=now_ns()+30000000000ULL;
  while(!j->done && state==2){uint64_t now=now_ns();if(now>=deadline)break;condvarWaitTimeout(&changed,&gate,deadline-now);}
  int ok=j->done;if(ok){*raw=j->response;*size=j->response_size;j->response=NULL;}
  mutexUnlock(&gate);release_job(j);return ok;
}
#else
#include <errno.h>
int m3_local_start(void){return 0;}
int m3_local_socket(int b){(void)b;errno=ENOSYS;return -1;}
int m3_local_connect(int f){(void)f;return -1;}
int m3_local_close(int f){(void)f;return -1;}
int m3_local_fcntl(int f,int c,long a){(void)f;(void)c;(void)a;return -1;}
int m3_local_ioctl(int f,unsigned long r,void *a){(void)f;(void)r;(void)a;return -1;}
short m3_local_ready(int f,short e){(void)f;(void)e;return 32;}
ssize_t m3_local_send(int f,const void *b,size_t n,int x){(void)f;(void)b;(void)n;(void)x;return -1;}
ssize_t m3_local_recv(int f,void *b,size_t n,int x){(void)f;(void)b;(void)n;(void)x;return -1;}
int m3_local_getsockopt(int f,int o,void *v,unsigned *n){(void)f;(void)o;(void)v;(void)n;return -1;}
int m3_local_setsockopt(int f,int l,int o,const void *v,unsigned n){(void)f;(void)l;(void)o;(void)v;(void)n;return -1;}
int m3_local_shutdown(int f,int h){(void)f;(void)h;return -1;}
int m3_local_http(const char *p,const void *b,size_t n,char **r,size_t *s){(void)p;(void)b;(void)n;(void)r;(void)s;return 0;}
#endif

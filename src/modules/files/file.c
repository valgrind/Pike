/*\
||| This file a part of Pike, and is copyright by Fredrik Hubinette
||| Pike is distributed as GPL (General Public License)
||| See the files COPYING and DISCLAIMER for more information.
\*/
#define READ_BUFFER 8192

#include "global.h"
RCSID("$Id: file.c,v 1.37.2.3 1997/06/25 22:47:56 hubbe Exp $");
#include "types.h"
#include "interpret.h"
#include "svalue.h"
#include "stralloc.h"
#include "array.h"
#include "object.h"
#include "pike_macros.h"
#include "backend.h"
#include "fd_control.h"
#include "module_support.h"
#include "gc.h"
#include "opcodes.h"

#include "file_machine.h"
#include "file.h"
#include "error.h"
#include "signal_handler.h"
#include "pike_types.h"
#include "threads.h"

#ifdef HAVE_SYS_TYPE_H
#include <sys/types.h>
#endif

#include <sys/stat.h>
#include <sys/param.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/socket.h>

#ifdef HAVE_SYS_STREAM_H
#include <sys/stream.h>

/* Ugly patch for AIX 3.2 */
#ifdef u
#undef u
#endif

#endif

#ifdef HAVE_SYS_PROTOSW_H
#include <sys/protosw.h>
#endif

#ifdef HAVE_SYS_SOCKETVAR_H
#include <sys/socketvar.h>
#endif

#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif

#ifndef SEEK_SET
#define SEEK_SET 0
#endif

#ifndef SEEK_CUR
#define SEEK_CUR 1
#endif

#ifndef SEEK_END
#define SEEK_END 2
#endif

struct file_struct
{
  short fd;
  short my_errno;
};

#define FD (((struct file_struct *)(fp->current_storage))->fd)
#define ERRNO (((struct file_struct *)(fp->current_storage))->my_errno)
#define THIS (files + FD)

static struct my_file files[MAX_OPEN_FILEDESCRIPTORS];
static struct program *file_program;

static void file_read_callback(int fd, void *data);
static void file_write_callback(int fd, void *data);

static void init_fd(int fd, int open_mode)
{
  files[fd].refs=1;
  files[fd].fd=fd;
  files[fd].open_mode=open_mode;
  files[fd].id.type=T_INT;
  files[fd].id.u.integer=0;
  files[fd].read_callback.type=T_INT;
  files[fd].read_callback.u.integer=0;
  files[fd].write_callback.type=T_INT;
  files[fd].write_callback.u.integer=0;
  files[fd].close_callback.type=T_INT;
  files[fd].close_callback.u.integer=0;
}

static int close_fd(int fd)
{
#ifdef DEBUG
  if(fd < 0)
    fatal("Bad argument to close_fd()\n");

  if(files[fd].refs<0)
    fatal("Wrong ref count in file struct\n");
#endif

  files[fd].refs--;
  if(!files[fd].refs)
  {
    while(1)
    {
      int i;
      THREADS_ALLOW();
      i=close(fd);
      THREADS_DISALLOW();
      
      if(i < 0)
      {
	switch(errno)
	{
	default:
	  /* What happened? */
	  /* files[fd].errno=errno; */

	  /* Try waiting it out in blocking mode */
	  set_nonblocking(fd,0);
	  THREADS_ALLOW();
	  i=close(fd);
	  THREADS_DISALLOW();
	  if(i>= 0 || errno==EBADF)  break; /* It was actually closed, good! */
	  
	  /* Failed, give up, crash, burn, die */
	  error("Failed to close file.\n");

	case EBADF:
	  error("Internal error: Closing a non-active file descriptor %d.\n",fd);
	  
	case EINTR:
	  continue;
	}
      }
      break;
    }

    set_read_callback(fd,0,0);
    set_write_callback(fd,0,0);

    free_svalue(& files[fd].id);
    free_svalue(& files[fd].read_callback);
    free_svalue(& files[fd].write_callback);
    free_svalue(& files[fd].close_callback);
    files[fd].id.type=T_INT;
    files[fd].read_callback.type=T_INT;
    files[fd].write_callback.type=T_INT;
    files[fd].close_callback.type=T_INT;
    files[fd].open_mode = 0;
  }
  return 0;
}

void my_set_close_on_exec(int fd, int to)
{
  if(to)
  {
    files[fd].open_mode |= FILE_SET_CLOSE_ON_EXEC;
  }else{
    if(files[fd].open_mode & FILE_SET_CLOSE_ON_EXEC)
      files[fd].open_mode &=~ FILE_SET_CLOSE_ON_EXEC;
    else
      set_close_on_exec(fd, 0);
  }
}

void do_set_close_on_exec()
{
  int e;
  for(e=0;e<MAX_OPEN_FILEDESCRIPTORS;e++)
  {
    if(files[e].open_mode & FILE_SET_CLOSE_ON_EXEC)
    {
      set_close_on_exec(e, 1);
      files[e].open_mode &=~ FILE_SET_CLOSE_ON_EXEC;
    }
  }
}

/* Parse "rw" to internal flags */
static int parse(char *a)
{
  int ret;
  ret=0;
  while(1)
  {
    switch(*(a++))
    {
    case 0: return ret;

    case 'r':
    case 'R':
      ret|=FILE_READ;
      break;

    case 'w':
    case 'W':
      ret|=FILE_WRITE;
      break; 

    case 'a':
    case 'A':
      ret|=FILE_APPEND;
      break;

    case 'c':
    case 'C':
      ret|=FILE_CREATE;
      break;

    case 't':
    case 'T':
      ret|=FILE_TRUNC;
      break;

    case 'x':
    case 'X':
      ret|=FILE_EXCLUSIVE;
      break;

   }
  }
}

/* Translate internal flags to open(2) modes */
static int map(int flags)
{
  int ret;
  ret=0;
  switch(flags & (FILE_READ|FILE_WRITE))
  {
  case FILE_READ: ret=O_RDONLY; break;
  case FILE_WRITE: ret=O_WRONLY; break;
  case FILE_READ | FILE_WRITE: ret=O_RDWR; break;
  }
  if(flags & FILE_APPEND) ret|=O_APPEND;
  if(flags & FILE_CREATE) ret|=O_CREAT;
  if(flags & FILE_TRUNC) ret|=O_TRUNC;
  if(flags & FILE_EXCLUSIVE) ret|=O_EXCL;
  return ret;
}

static void call_free(char *s) { free(s); }
static void free_dynamic_buffer(dynamic_buffer *b) { free(b->s.str); }

static struct pike_string *do_read(int fd,
				   INT32 r,
				   int all,
				   short *err)
{
  ONERROR ebuf;
  INT32 bytes_read,i;
  bytes_read=0;
  *err=0;

  if(r < 65536)
  {
    struct pike_string *str;

    str=begin_shared_string(r);

    SET_ONERROR(ebuf, call_free, str);

    do{
      int fd=FD;
      THREADS_ALLOW();
      i=read(fd, str->str+bytes_read, r);
      THREADS_DISALLOW();

      check_signals(0,0,0);

      if(i>0)
      {
	r-=i;
	bytes_read+=i;
	if(!all) break;
      }
      else if(i==0)
      {
	break;
      }
      else if(errno != EINTR)
      {
	*err=errno;
	if(!bytes_read)
	{
	  free((char *)str);
	  UNSET_ONERROR(ebuf);
	  return 0;
	}
	break;
      }
    }while(r);

    UNSET_ONERROR(ebuf);
    
    if(bytes_read == str->len)
    {
      return end_shared_string(str);
    }else{
      struct pike_string *foo; /* Per */
      foo = make_shared_binary_string(str->str,bytes_read);
      free((char *)str);
      return foo;
    }
    
  }else{
#define CHUNK 65536
    INT32 try_read;
    dynamic_buffer b;

    b.s.str=0;
    initialize_buf(&b);
    SET_ONERROR(ebuf, free_dynamic_buffer, &b);
    do{
      try_read=MINIMUM(CHUNK,r);
      
      THREADS_ALLOW();
      i=read(fd, low_make_buf_space(try_read, &b), try_read);
      THREADS_DISALLOW();

      check_signals(0,0,0);
      
      if(i==try_read)
      {
	r-=i;
	bytes_read+=i;
	if(!all) break;
      }
      else if(i>0)
      {
	bytes_read+=i;
	r-=i;
	low_make_buf_space(i - try_read, &b);
	if(!all) break;
      }
      else if(i==0)
      {
	low_make_buf_space(-try_read, &b);
	break;
      }
      else
      {
	low_make_buf_space(-try_read, &b);
	if(errno != EINTR)
	{
	  *err=errno;
	  if(!bytes_read)
	  {
	    free(b.s.str);
	    UNSET_ONERROR(ebuf);
	    return 0;
	  }
	  break;
	}
      }
    }while(r);

    UNSET_ONERROR(ebuf);
    return low_free_buf(&b);
  }
}

static void file_read(INT32 args)
{
  struct pike_string *tmp;
  INT32 all, len;

  if(FD < 0)
    error("File not open.\n");

  if(!args)
  {
    len=0x7fffffff;
  }
  else
  {
    if(sp[-args].type != T_INT)
      error("Bad argument 1 to file->read().\n");
    len=sp[-args].u.integer;
  }

  if(args > 1 && !IS_ZERO(sp+1-args))
  {
    all=0;
  }else{
    all=1;
  }

  pop_n_elems(args);

  if((tmp=do_read(FD, len, all, & ERRNO)))
    push_string(tmp);
  else
    push_int(0);
}

static void file_write_callback(int fd, void *data)
{
  set_write_callback(fd, 0, 0);

  assign_svalue_no_free(sp++, & files[fd].id);
  apply_svalue(& files[fd].write_callback, 1);
  pop_stack();
}

static void file_write(INT32 args)
{
  INT32 written,i;
  struct pike_string *str;

  if(args<1 || sp[-args].type != T_STRING)
    error("Bad argument 1 to file->write().\n");

  if(FD < 0)
    error("File not open for write.\n");
  
  written=0;
  str=sp[-args].u.string;

  while(written < str->len)
  {
    int fd=FD;
    THREADS_ALLOW();
    i=write(fd, str->str + written, str->len - written);
    THREADS_DISALLOW();

    if(i<0)
    {
      switch(errno)
      {
      default:
	ERRNO=errno;
	pop_n_elems(args);
	push_int(-1);
	return;

      case EINTR: continue;
      case EWOULDBLOCK: break;
      }
      break;
    }else{
      written+=i;

      /* Avoid extra write() */
      if(THIS->open_mode & FILE_NONBLOCKING)
	break;
    }
  }

  if(!IS_ZERO(& THIS->write_callback))
    set_write_callback(FD, file_write_callback, 0);
  ERRNO=0;

  pop_n_elems(args);
  push_int(written);
}

static int do_close(int fd, int flags)
{
  if(fd == -1) return 1; /* already closed */

  /* files[fd].errno=0; */

  if(!(files[fd].open_mode & (FILE_READ | FILE_WRITE)))
    return 1;

  flags &= files[fd].open_mode;

  switch(flags & (FILE_READ | FILE_WRITE))
  {
  case 0:
    return 0;

  case FILE_READ:
    if(files[fd].open_mode & FILE_WRITE)
    {
      set_read_callback(fd,0,0);
      shutdown(fd, 0);
      files[fd].open_mode &=~ FILE_READ;
      return 0;
    }else{
      close_fd(fd);
      return 1;
    }

  case FILE_WRITE:
    if(files[fd].open_mode & FILE_READ)
    {
      set_write_callback(fd,0,0);
      shutdown(fd, 1);
      files[fd].open_mode &=~ FILE_WRITE;
      return 0;
    }else{
      close_fd(fd);
      return 1;
    }

  case FILE_READ | FILE_WRITE:
    close_fd(fd);
    return 1;

  default:
    fatal("Bug in switch implementation!\n");
    return 0; /* Make CC happy */
  }
}

static void file_close(INT32 args)
{
  int flags;
  if(args)
  {
    if(sp[-args].type != T_STRING)
      error("Bad argument 1 to file->close()\n");
    flags=parse(sp[-args].u.string->str);
  }else{
    flags=FILE_READ | FILE_WRITE;
  }

  if(do_close(FD,flags))
    FD=-1;
  pop_n_elems(args);
  push_int(1);
}

static void file_open(INT32 args)
{
  int flags,fd;
  struct pike_string *str;
  do_close(FD, FILE_READ | FILE_WRITE);
  FD=-1;
  
  if(args < 2)
    error("Too few arguments to file->open()\n");

  if(sp[-args].type != T_STRING)
    error("Bad argument 1 to file->open()\n");

  if(sp[1-args].type != T_STRING)
    error("Bad argument 2 to file->open()\n");

  str=sp[-args].u.string;
  
  flags=parse(sp[1-args].u.string->str);

  if(!( flags &  (FILE_READ | FILE_WRITE)))
    error("Must open file for at least one of read and write.\n");

  THREADS_ALLOW();
  do {
    fd=open(str->str,map(flags), 00666);
  } while(fd < 0 && errno == EINTR);
  THREADS_DISALLOW();

  if(!fp->current_object->prog)
    error("Object destructed in file->open()\n");

  if(fd >= MAX_OPEN_FILEDESCRIPTORS)
  {
    ERRNO=EBADF;
    close(fd);
    fd=-1;
  }
  else if(fd < 0)
  {
    ERRNO=errno;
  }
  else
  {
    init_fd(fd,flags);
    FD=fd;
    ERRNO = 0;
    set_close_on_exec(fd,1);
  }

  pop_n_elems(args);
  push_int(fd>=0);
}


static void file_seek(INT32 args)
{
  INT32 to;

  if(args<1 || sp[-args].type != T_INT)
    error("Bad argument 1 to file->seek().\n");

  if(FD < 0)
    error("File not open.\n");
  
  to=sp[-args].u.integer;

  ERRNO=0;

  to=lseek(FD,to,to<0 ? SEEK_END : SEEK_SET);

  if(to<0) ERRNO=errno;

  pop_n_elems(args);
  push_int(to);
}

static void file_tell(INT32 args)
{
  INT32 to;

  if(FD < 0)
    error("File not open.\n");
  
  ERRNO=0;
  to=lseek(FD, 0L, SEEK_CUR);

  if(to<0) ERRNO=errno;

  pop_n_elems(args);
  push_int(to);
}

struct array *encode_stat(struct stat *);

static void file_stat(INT32 args)
{
  int fd;
  struct stat s;
  int tmp;

  if(FD < 0)
    error("File not open.\n");
  
  pop_n_elems(args);

  fd=FD;

 retry:
  THREADS_ALLOW();
  tmp=fstat(fd, &s);
  THREADS_DISALLOW();

  if(tmp < 0)
  {
    if(errno == EINTR) goto retry;
    ERRNO=errno;
    push_int(0);
  }else{
    ERRNO=0;
    push_array(encode_stat(&s));
  }
}

static void file_errno(INT32 args)
{
  pop_n_elems(args);
  push_int(ERRNO);
}

/* Trick compiler to keep 'buffer' in memory for
 * as short a time as possible.
 * shouldn't be any need to allow threading here, this
 * call should never block..
 */
static struct pike_string *simple_do_read(INT32 *amount,int fd)
{
  char buffer[READ_BUFFER];
  *amount = read(fd, buffer, READ_BUFFER);
  if(*amount>0) return make_shared_binary_string(buffer,*amount);
  return 0;
}

static void file_read_callback(int fd, void *data)
{
  struct pike_string *s;
  INT32 i;

#ifdef DEBUG
  if(fd == -1 || fd >= MAX_OPEN_FILEDESCRIPTORS)
    fatal("Error in file::read_callback()\n");
#endif

  /* files[fd].errno=0; */

  s=simple_do_read(&i, fd);

  if(i>0)
  {
    assign_svalue_no_free(sp++, &files[fd].id);
    push_string(s);
    apply_svalue(& files[fd].read_callback, 2);
    pop_stack();
    return;
  }
  
  if(i < 0)
  {
    /* files[fd].errno=errno; */
    switch(errno)
    {
    case EINTR:
    case EWOULDBLOCK:
      return;
    }
  }

  set_read_callback(fd, 0, 0);

  /* We _used_ to close the file here, not possible anymore though... */
  /* Hmm, I wonder why... :P */
  assign_svalue_no_free(sp++, &files[fd].id);
  apply_svalue(& files[fd].close_callback, 1);
}

static void file_set_read_callback(INT32 args)
{
  if(FD < 0)
    error("File is not open.\n");

  if(args < 1)
    error("Too few arguments to file->set_read_callback().\n");

  assign_svalue(& THIS->read_callback, sp-args);

  if(IS_ZERO(& THIS->read_callback))
  {
    set_read_callback(FD, 0, 0);
  }else{
    set_read_callback(FD, file_read_callback, 0);
  }
  pop_n_elems(args);
}

static void file_set_write_callback(INT32 args)
{
  if(FD < 0)
    error("File is not open.\n");

  if(args < 1)
    error("Too few arguments to file->set_write_callback().\n");

  assign_svalue(& THIS->write_callback, sp-args);

  if(IS_ZERO(& THIS->write_callback))
  {
    set_write_callback(FD, 0, 0);
  }else{
    set_write_callback(FD, file_write_callback, 0);
  }
  pop_n_elems(args);
}

static void file_set_close_callback(INT32 args)
{
  if(FD < 0)
    error("File is not open.\n");

  if(args < 1)
    error("Too few arguments to file->set_close_callback().\n");

  assign_svalue(& THIS->close_callback, sp-args);
  pop_n_elems(args);
}

static void file_set_nonblocking(INT32 args)
{
  if(FD < 0) error("File not open.\n");

  switch(args)
  {
  default: pop_n_elems(args-3);
  case 3: file_set_close_callback(1);
  case 2: file_set_write_callback(1);
  case 1: file_set_read_callback(1);
  case 0: break;
  }
  set_nonblocking(FD,1);
  THIS->open_mode |= FILE_NONBLOCKING;
}


static void file_set_blocking(INT32 args)
{
  free_svalue(& THIS->read_callback);
  THIS->read_callback.type=T_INT;
  THIS->read_callback.u.integer=0;
  free_svalue(& THIS->write_callback);
  THIS->write_callback.type=T_INT;
  THIS->write_callback.u.integer=0;
  free_svalue(& THIS->close_callback);
  THIS->close_callback.type=T_INT;
  THIS->close_callback.u.integer=0;

  if(FD >= 0)
  {
    set_read_callback(FD, 0, 0);
    set_write_callback(FD, 0, 0);
    set_nonblocking(FD,0);
    THIS->open_mode &=~ FILE_NONBLOCKING;
  }
  pop_n_elems(args);
}

static void file_set_close_on_exec(INT32 args)
{
  if(args < 0)
    error("Too few arguments to file->set_close_on_exec()\n");
  if(FD <0)
    error("File not open.\n");

  if(IS_ZERO(sp-args))
  {
    my_set_close_on_exec(FD,0);
  }else{
    my_set_close_on_exec(FD,1);
  }
  pop_n_elems(args-1);
}

static void file_set_id(INT32 args)
{
  if(args < 1)
    error("Too few arguments to file->set_id()\n");

  if(FD < 0)
    error("File not open.\n");

  assign_svalue(& THIS->id, sp-args);
  pop_n_elems(args-1);
}

static void file_query_id(INT32 args)
{
  if(FD < 0)
    error("File not open.\n");

  pop_n_elems(args);
  assign_svalue_no_free(sp++,& THIS->id);
}

static void file_query_fd(INT32 args)
{
  if(FD < 0)
    error("File not open.\n");

  pop_n_elems(args);
  push_int(FD);
}

static void file_query_read_callback(INT32 args)
{
  if(FD < 0)
    error("File not open.\n");

  pop_n_elems(args);
  assign_svalue_no_free(sp++,& THIS->read_callback);
}

static void file_query_write_callback(INT32 args)
{
  if(FD < 0)
    error("File not open.\n");

  pop_n_elems(args);
  assign_svalue_no_free(sp++,& THIS->write_callback);
}

static void file_query_close_callback(INT32 args)
{
  if(FD < 0)
    error("File not open.\n");

  pop_n_elems(args);
  assign_svalue_no_free(sp++,& THIS->close_callback);
}

struct object *file_make_object_from_fd(int fd, int mode)
{
  struct object *o;

  init_fd(fd, mode);
  o=clone_object(file_program,0);
  ((struct file_struct *)(o->storage))->fd=fd;
  ((struct file_struct *)(o->storage))->my_errno=0;
  return o;
}

static void file_set_buffer(INT32 args)
{
  INT32 bufsize;
  int flags;

  if(FD==-1)
    error("file->set_buffer() on closed file.\n");
  if(!args)
    error("Too few arguments to file->set_buffer()\n");
  if(sp[-args].type!=T_INT)
    error("Bad argument 1 to file->set_buffer()\n");

  bufsize=sp[-args].u.integer;
  if(bufsize < 0)
    error("Bufsize must be larger than zero.\n");

  if(args>1)
  {
    if(sp[1-args].type != T_STRING)
      error("Bad argument 2 to file->set_buffer()\n");
    flags=parse(sp[1-args].u.string->str);
  }else{
    flags=FILE_READ | FILE_WRITE;
  }

#ifdef SOCKET_BUFFER_MAX
#if SOCKET_BUFFER_MAX
  if(bufsize>SOCKET_BUFFER_MAX) bufsize=SOCKET_BUFFER_MAX;
#endif
  flags &= files[FD].open_mode;
  if(flags & FILE_READ)
  {
    int tmp=bufsize;
    setsockopt(FD,SOL_SOCKET, SO_RCVBUF, (char *)&tmp, sizeof(tmp));
  }

  if(flags & FILE_WRITE)
  {
    int tmp=bufsize;
    setsockopt(FD,SOL_SOCKET, SO_SNDBUF, (char *)&tmp, sizeof(tmp));
  }
#endif
}


#ifndef HAVE_SOCKETPAIR

/* No socketpair() ?
 * No AF_UNIX sockets ?
 * No hope ?
 *
 * Don't dispair, socketpair_ultra is here!
 * Tests done in an independant institute in europe shows
 * socketpair_ultra is 50% more portable than other leading
 * brands of socketpair.
 *                                                   /Hubbe
 */

/* redefine socketpair to something that hopefully won't
 * collide with any libs or headers. Also useful when testing
 * this code on a system that _has_ socketpair...
 */
#define socketpair socketpair_ultra

extern int errno;
int socketpair(int family, int type, int protocol, int sv[2])
{
  struct sockaddr_in addr,addr2;
  int len, fd;

  MEMSET((char *)&addr,0,sizeof(struct sockaddr_in));

  /* We lie, we actually create an AF_INET socket... */
  if(family != AF_UNIX || type != SOCK_STREAM)
  {
    errno=EINVAL;
    return -1; 
  }
  
  if((fd=socket(AF_INET, SOCK_STREAM, 0)) < 0) return -1;
  if((sv[1]=socket(AF_INET, SOCK_STREAM, 0)) <0) return -1;


  /* I wonder what is most common a loopback on ip# 127.0.0.1 or
   * a loopback with the name "localhost"?
   * Let's hope those few people who doesn't have socketpair has
   * a loopback on 127.0.0.1
   */
  addr.sin_addr.s_addr=htonl(INADDR_ANY);
  addr.sin_port=htons(0);

  /* Bind our sockets on any port */
  if(bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) return -1;

  /* Check what ports we got.. */
  len=sizeof(addr);
  if(getsockname(fd,(struct sockaddr *)&addr,&len) < 0) return -1;

  addr.sin_addr.s_addr=inet_addr("127.0.0.1");

  /* Listen to connections on our new socket */
  if(listen(fd, 3) < 0 ) return -1;

/*  set_nonblocking(sv[1],1); */
  
  /* Connect */
  if(connect(sv[1], (struct sockaddr *)&addr, sizeof(addr)) < 0) return -1;

  len=sizeof(addr);
  if(getsockname(sv[1],(struct sockaddr *)&addr2,&len) < 0) return -1;
  

  /* Accept connection
   * Make sure this connection was our OWN connection,
   * otherwise some wizeguy could interfere with our
   * pipe by guessing our socket and connecting at
   * just the right time... Pike is supposed to be
   * pretty safe...
   */
  do
  {
    len=sizeof(addr);
    sv[0]=accept(fd,(struct sockaddr *)&addr,&len);
    if(sv[0] < 0) return -1;

    /* We do not trust accept */
    len=sizeof(addr);
    if(getpeername(sv[0], (struct sockaddr *)&addr,&len)) return -1;
  }while(len < (int)sizeof(addr) ||
	 addr2.sin_addr.s_addr != addr.sin_addr.s_addr ||
	 addr2.sin_port != addr.sin_port);

  if(close(fd) <0) return -1;

/*   set_nonblocking(sv[1],0); */

  return 0;
}

#endif

static void file_pipe(INT32 args)
{
  int inout[2],i;
  do_close(FD,FILE_READ | FILE_WRITE);
  FD=-1;
  pop_n_elems(args);
  ERRNO=0;

  i=socketpair(AF_UNIX, SOCK_STREAM, 0, &inout[0]);
  if(i<0)
  {
    ERRNO=errno;
    push_int(0);
  }
  else if(i >= MAX_OPEN_FILEDESCRIPTORS)
  {
    ERRNO=EBADF;
    close(i);
    push_int(0);
  }
  else
  {
    init_fd(inout[0],FILE_READ | FILE_WRITE);

    my_set_close_on_exec(inout[0],1);
    my_set_close_on_exec(inout[1],1);
    FD=inout[0];

    ERRNO=0;
    push_object(file_make_object_from_fd(inout[1],FILE_READ | FILE_WRITE));
  }
}


static void init_file_struct(struct object *o)
{
  FD=-1;
  ERRNO=-1;
}

static void exit_file_struct(struct object *o)
{
  do_close(FD,FILE_READ | FILE_WRITE);
  ERRNO=-1;
}

static void file_dup(INT32 args)
{
  struct object *o;

  if(FD < 0)
    error("File not open.\n");

  pop_n_elems(args);

  o=clone_object(file_program,0);
  ((struct file_struct *)o->storage)->fd=FD;
  ((struct file_struct *)o->storage)->my_errno=0;
  ERRNO=0;
  files[FD].refs++;
  push_object(o);
}

static void file_assign(INT32 args)
{
  struct object *o;

  if(args < 1)
    error("Too few arguments to file->assign()\n");

  if(sp[-args].type != T_OBJECT)
    error("Bad argument 1 to file->assign()\n");

  o=sp[-args].u.object;

  /* Actually, we allow any object which first inherit is 
   * /precompiled/file
   */
  if(!o->prog || o->prog->inherits[0].prog != file_program)
    error("Argument 1 to file->assign() must be a clone of Stdio.File\n");
  do_close(FD, FILE_READ | FILE_WRITE);

  FD=((struct file_struct *)(o->storage))->fd;
  ERRNO=0;
  if(FD >=0) files[FD].refs++;

  pop_n_elems(args);
  push_int(1);
}

static void file_dup2(INT32 args)
{
  int fd;
  struct object *o;

  if(args < 1)
    error("Too few arguments to file->dup2()\n");

  if(FD < 0)
    error("File not open.\n");

  if(sp[-args].type != T_OBJECT)
    error("Bad argument 1 to file->dup2()\n");

  o=sp[-args].u.object;

  /* Actually, we allow any object which first inherit is 
   * /precompiled/file
   */
  if(!o->prog || o->prog->inherits[0].prog != file_program)
    error("Argument 1 to file->assign() must be a clone of Stdio.File\n");

  fd=((struct file_struct *)(o->storage))->fd;

  if(fd < 0)
    error("File given to dup2 not open.\n");

  if(dup2(FD,fd) < 0)
  {
    ERRNO=errno;
    pop_n_elems(args);
    push_int(0);
    return;
  }
  ERRNO=0;
  my_set_close_on_exec(fd, fd > 2);
  files[fd].open_mode=files[FD].open_mode;

  assign_svalue_no_free(& files[fd].read_callback, & THIS->read_callback);
  assign_svalue_no_free(& files[fd].write_callback, & THIS->write_callback);
  assign_svalue_no_free(& files[fd].close_callback, & THIS->close_callback);
  assign_svalue_no_free(& files[fd].id, & THIS->id);

  if(IS_ZERO(& THIS->read_callback))
  {
    set_read_callback(fd, 0,0);
  }else{
    set_read_callback(fd, file_read_callback, 0);
  }
  
  if(IS_ZERO(& THIS->write_callback))
  {
    set_write_callback(fd, 0,0);
  }else{
    set_write_callback(fd, file_write_callback, 0);
  }
  
  pop_n_elems(args);
  push_int(1);
}

/* file->open_socket(int|void port, string|void addr) */
static void file_open_socket(INT32 args)
{
  int fd;

  do_close(FD, FILE_READ | FILE_WRITE);
  FD=-1;
  fd=socket(AF_INET, SOCK_STREAM, 0);
  if(fd >= MAX_OPEN_FILEDESCRIPTORS)
  {
    ERRNO=EBADF;
    pop_n_elems(args);
    push_int(0);
    return;
  }
  if(fd < 0)
  {
    ERRNO=errno;
    pop_n_elems(args);
    push_int(0);
    return;
  }

  if (args) {
    struct sockaddr_in addr;
    int o;

    if (sp[-args].type != T_INT) {
      close(fd);
      error("Bad argument 1 to open_socket(), expected int\n");
    }
    if (args > 1) {
      if (sp[1-args].type != T_STRING) {
	close(fd);
	error("Bad argument 2 to open_socket(), expected string\n");
      }
      get_inet_addr(&addr, sp[1-args].u.string->str);
    } else {
      addr.sin_addr.s_addr = htonl(INADDR_ANY);
      addr.sin_family = AF_INET;
    }
    addr.sin_port = htons( ((u_short)sp[-args].u.integer) );

    o=1;
    if(setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char *)&o, sizeof(int)) < 0) {
      ERRNO=errno;
      close(fd);
      pop_n_elems(args);
      push_int(0);
      return;
    }
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
      ERRNO=errno;
      close(fd);
      pop_n_elems(args);
      push_int(0);
      return;
    }
  }

  init_fd(fd, FILE_READ | FILE_WRITE);
  my_set_close_on_exec(fd,1);
  FD = fd;
  ERRNO=0;

  pop_n_elems(args);
  push_int(1);
}

static void file_set_keepalive(INT32 args)
{
  int tmp, i;
  check_all_args("file->set_keepalive",args, T_INT,0);
  tmp=sp[-args].u.integer;
  i=setsockopt(FD,SOL_SOCKET, SO_KEEPALIVE, (char *)&tmp, sizeof(tmp));
  if(i)
  {
    ERRNO=errno;
  }else{
    ERRNO=0;
  }
  pop_n_elems(args);
  push_int(!i);
}

static void file_connect(INT32 args)
{
  struct sockaddr_in addr;
  int tmp;
  if(args < 2)
    error("Too few arguments to file->connect()\n");

  if(sp[-args].type != T_STRING)
    error("Bad argument 1 to file->connect()\n");
      
  if(sp[1-args].type != T_INT)
    error("Bad argument 2 to file->connect()\n");

  if(FD < 0)
  {
    file_open_socket(0);
    if(IS_ZERO(sp-1) || FD < 0)
      error("file->connect(): Failed to open socket.\n");
    pop_stack();
  }


  get_inet_addr(&addr, sp[-args].u.string->str);
  addr.sin_port = htons(((u_short)sp[1-args].u.integer));

  tmp=FD;
  THREADS_ALLOW();
  tmp=connect(tmp, (struct sockaddr *)&addr, sizeof(addr));
  THREADS_DISALLOW();

  if(tmp < 0)
  {
    /* something went wrong */
    ERRNO=errno;
    pop_n_elems(args);
    push_int(0);
  }else{

    ERRNO=0;
    pop_n_elems(args);
    push_int(1);
  }
}

static int isipnr(char *s)
{
  int e,i;
  for(e=0;e<3;e++)
  {
    i=0;
    while(*s==' ') s++;
    while(*s>='0' && *s<='9') s++,i++;
    if(!i) return 0;
    if(*s!='.') return 0;
    s++;
  }
  i=0;
  while(*s==' ') s++;
  while(*s>='0' && *s<='9') s++,i++;
  if(!i) return 0;
  while(*s==' ') s++;
  if(*s) return 0;
  return 1;
}

static void file_query_address(INT32 args)
{
  struct sockaddr_in addr;
  int i,len;
  char buffer[496],*q;

  if(FD <0)
    error("file->query_address(): Connection not open.\n");

  len=sizeof(addr);
  if(args > 0 && !IS_ZERO(sp-args))
  {
    i=getsockname(FD,(struct sockaddr *)&addr,&len);
  }else{
    i=getpeername(FD,(struct sockaddr *)&addr,&len);
  }
  pop_n_elems(args);
  if(i < 0 || len < (int)sizeof(addr))
  {
    ERRNO=errno;
    push_int(0);
    return;
  }

  q=inet_ntoa(addr.sin_addr);
  strncpy(buffer,q,sizeof(buffer)-20);
  buffer[sizeof(buffer)-20]=0;
  sprintf(buffer+strlen(buffer)," %d",(int)(ntohs(addr.sin_port)));

  push_string(make_shared_string(buffer));
}

static void file_lsh(INT32 args)
{
  INT32 len;
  if(args != 1)
    error("Too few/many args to file->`<<\n");

  if(sp[-1].type != T_STRING)
  {
    push_string(string_type_string);
    string_type_string->refs++;
    f_cast();
  }

  len=sp[-1].u.string->len;
  file_write(1);
  if(len != sp[-1].u.integer) error("File << failed.\n");
  pop_stack();

  push_object(this_object());
}

static void file_create(INT32 args)
{
  char *s;
  if(!args || sp[-args].type == T_INT) return;
  if(sp[-args].type != T_STRING)
    error("Bad argument 1 to file->create()\n");

  do_close(FD, FILE_READ | FILE_WRITE);
  FD=-1;
  s=sp[-args].u.string->str;
  if(!strcmp(s,"stdin"))
  {
    FD=0;
    files[0].refs++;
  }
  else if(!strcmp(s,"stdout"))
  {
    FD=1;
    files[1].refs++;
  }
  else if(!strcmp(s,"stderr"))
  {
    FD=2;
    files[2].refs++;
  }
  else
  {
    file_open(args); /* Try opening the file instead. */
  }
}

void pike_module_exit()
{
  if(file_program)
  {
    free_program(file_program);
    file_program=0;
  }
}

void mark_ids(struct callback *foo, void *bar, void *gazonk)
{
  int e;
  for(e=0;e<MAX_OPEN_FILEDESCRIPTORS;e++)
  {
    gc_check_svalues( & files[e].id, 1);
    gc_check_svalues( & files[e].close_callback, 1);
  }
}

void init_files_efuns(void);

void pike_module_init()
{
  extern void port_setup_program(void);
  int e;
  for(e=0;e<MAX_OPEN_FILEDESCRIPTORS;e++)
  {
    init_fd(e, 0);
    files[e].refs=0;
  }

  init_fd(0, FILE_READ);
  init_fd(1, FILE_WRITE);
  init_fd(2, FILE_WRITE);

  init_files_efuns();

  start_new_program();
  add_storage(sizeof(struct file_struct));

  add_function("open",file_open,"function(string,string:int)",0);
  add_function("close",file_close,"function(string|void:int)",0);
  add_function("read",file_read,"function(int|void,int|void:int|string)",0);
  add_function("write",file_write,"function(string:int)",0);

  add_function("seek",file_seek,"function(int:int)",0);
  add_function("tell",file_tell,"function(:int)",0);
  add_function("stat",file_stat,"function(:int *)",0);
  add_function("errno",file_errno,"function(:int)",0);

  add_function("set_close_on_exec",file_set_close_on_exec,"function(int:void)",0);
  add_function("set_nonblocking",file_set_nonblocking,"function(mixed|void,mixed|void,mixed|void:void)",0);
  add_function("set_read_callback",file_set_read_callback,"function(mixed:void)",0);
  add_function("set_write_callback",file_set_write_callback,"function(mixed:void)",0);
  add_function("set_close_callback",file_set_close_callback,"function(mixed:void)",0);

  add_function("set_blocking",file_set_blocking,"function(:void)",0);
  add_function("set_id",file_set_id,"function(mixed:void)",0);

  add_function("query_fd",file_query_fd,"function(:int)",0);
  add_function("query_id",file_query_id,"function(:mixed)",0);
  add_function("query_read_callback",file_query_read_callback,"function(:mixed)",0);
  add_function("query_write_callback",file_query_write_callback,"function(:mixed)",0);
  add_function("query_close_callback",file_query_close_callback,"function(:mixed)",0);

  add_function("dup",file_dup,"function(:object)",0);
  add_function("dup2",file_dup2,"function(object:object)",0);
  add_function("assign",file_assign,"function(object:int)",0);
  add_function("pipe",file_pipe,"function(:object)",0);

  add_function("set_buffer",file_set_buffer,"function(int,string|void:void)",0);
  add_function("open_socket",file_open_socket,"function(int|void,string|void:int)",0);
  add_function("connect",file_connect,"function(string,int:int)",0);
  add_function("query_address",file_query_address,"function(int|void:string)",0);
  add_function("create",file_create,"function(void|string,void|string:void)",0);
  add_function("`<<",file_lsh,"function(mixed:object)",0);

  set_init_callback(init_file_struct);
  set_exit_callback(exit_file_struct);

  file_program=end_program();
  add_program_constant("file",file_program,0);

  port_setup_program();
  
  add_gc_callback(mark_ids, 0, 0);
}

/* Used from backend */
int pike_make_pipe(int *fds)
{
  return socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
}

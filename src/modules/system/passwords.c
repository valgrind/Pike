/*
 * $Id: passwords.c,v 1.7 1998/07/04 16:52:53 grubba Exp $
 *
 * Password handling for Pike.
 *
 * Henrik Grubbström 1997-01-28
 */

/*
 * Includes
 */

#include "global.h"

#include "system_machine.h"
#include "system.h"

RCSID("$Id: passwords.c,v 1.7 1998/07/04 16:52:53 grubba Exp $");

#include "module_support.h"
#include "interpret.h"
#include "stralloc.h"
#include "threads.h"
#include "svalue.h"
#include "builtin_functions.h"

#ifdef HAVE_PASSWD_H
# include <passwd.h>
# include <group.h>
#endif /* HAVE_PASSWD_H */

#ifdef HAVE_PWD_H
# include <pwd.h>
#endif /* HAVE_PWD_H */

#ifdef HAVE_GRP_H
# include <grp.h>
#endif /* HAVE_GRP_H */

#ifdef HAVE_SHADOW_H
# include <shadow.h>
#endif /* HAVE_SHADOW_H */
 
/*
 * Functions
 */

#if defined(HAVE_GETPWNAM) || defined(HAVE_GETPWUID) || defined(HAVE_GETPWENT)
static void push_pwent(struct passwd *ent)
{
  if(!ent)
  {
    push_int(0);
    return;
  }
  push_text(ent->pw_name);
 
#ifdef HAVE_GETSPNAM
  if(!strcmp(ent->pw_passwd, "x"))
  {
    struct spwd *foo;
    if((foo = getspnam(ent->pw_name)))
      push_text(foo->sp_pwdp);
    else
      push_text("x");
  } else 
#endif /* Shadow password support */
    push_text(ent->pw_passwd);
  push_int(ent->pw_uid);
  push_int(ent->pw_gid);
  push_text(ent->pw_gecos);
  push_text(ent->pw_dir);
  push_text(ent->pw_shell);
  f_aggregate(7);
}
#endif

#if defined(HAVE_GETGRNAM) || defined(HAVE_GETGRUID) || defined(HAVE_GETGRENT)
static void push_grent(struct group *ent)
{
  if(!ent)
  {
    push_int(0);
    return;
  }
  push_text(ent->gr_name);
  push_text(ent->gr_passwd);
  push_int(ent->gr_gid);
  {
    char **cp = ent->gr_mem;
    int i=0;
    while(cp[i]) push_text(cp[i++]);
    f_aggregate(i);
  }
  f_aggregate(4);
}
#endif

#ifdef HAVE_GETGRGID
/* array getgrgid(int gid) */
void f_getgrgid(INT32 args)
{
  int gid;
  struct group *foo;
  get_all_args("getgrgid", args, "%d", &gid);
  foo = getgrgid( gid );
  pop_n_elems( args );
  push_grent( foo );
}
#endif /* HAVE_GETGRGID */

#ifdef HAVE_GETGRNAM
/* array getgrnam(string str) */
void f_getgrnam(INT32 args)
{
  char *str;
  struct group *foo;
  get_all_args("getgrnam", args, "%s", &str);
  foo = getgrnam( str );
  pop_n_elems( args );
  push_grent( foo );
}
#endif /* HAVE_GETGRNAM */

#ifdef HAVE_GETPWNAM
/* array getpwnam(string str) */
void f_getpwnam(INT32 args)
{
  char *str;
  struct passwd *foo;

  get_all_args("getpwnam", args, "%s", &str);

  foo = getpwnam(str);

  pop_n_elems(args);
  push_pwent(foo);
}
#endif /* HAVE_GETPWNAM */

#ifdef HAVE_GETPWUID
/* array getpwuid(int uid) */
void f_getpwuid(INT32 args)
{
  int uid;
  struct passwd *foo;
  
  get_all_args("getpwuid", args, "%i", &uid);

  foo = getpwuid(uid);

  pop_n_elems(args);
  push_pwent(foo);
}
#endif /* HAVE_GETPWUID */

#ifdef HAVE_SETPWENT
/* int setpwent() */
void f_setpwent(INT32 args)
{
  setpwent();
  pop_n_elems(args);
  push_int(0);
}
#endif /* HAVE_SETPWENT */
 
#ifdef HAVE_ENDPWENT
/* int endpwent() */
void f_endpwent(INT32 args)
{
  endpwent();
  pop_n_elems(args);
  push_int(0);
}
#endif /* HAVE_ENDPWENT */

#ifdef HAVE_GETPWENT
/* int|array getpwent() */ 
void f_getpwent(INT32 args)
{
  struct passwd *foo;
  pop_n_elems(args);
  foo = getpwent();
  if(!foo)
  {
    push_int(0);
    return;
  }
  push_pwent(foo);
}
#endif /* HAVE_GETPWENT */

#ifdef HAVE_SETGRENT
/* int setgrent() */
void f_setgrent(INT32 args)
{
  setgrent();
  pop_n_elems(args);
  push_int(0);
}
#endif /* HAVE_SETGRENT */

#ifdef HAVE_ENDGRENT
/* int endgrent() */
void f_endgrent(INT32 args)
{
  endgrent();
  pop_n_elems(args);
  push_int(0);
}
#endif /* HAVE_ENDGRENT */

#ifdef HAVE_GETGRENT
/* int|array getgrent() */
void f_getgrent(INT32 args)
{
  struct group *foo;
  pop_n_elems(args);
  foo = getgrent();
  if(!foo)
  {
    push_int(0);
    return;
  }
  push_grent(foo);
}
#endif /* HAVE_GETGRENT */

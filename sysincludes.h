/* System includes for mtools */

#ifndef SYSINCLUDES_H
#define SYSINCLUDES_H

#include "config.h"

#ifndef __GNUC__
#define __const const
#define UNUSED(x) /**/
#else
#define UNUSED(x) x __attribute__ ((unused))
#endif

#include <sys/types.h>

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#include <stdio.h>
#include <ctype.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef HAVE_GETOPT_H
#include <getopt.h>
#else
int getopt();
extern char *optarg;
extern int optind, opterr;
#endif

#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif

#ifdef HAVE_LIMITS_H
#include <limits.h>
#endif

#ifdef HAVE_SYS_FILE_H
#include <sys/file.h>
#endif

#ifdef HAVE_SYS_IOCTL_H
#ifndef sunos
#include <sys/ioctl.h>
#endif
#endif
/* if we don't have sys/ioctl.h, we rely on unistd to supply a prototype
 * for it. If it doesn't, we'll only get a (harmless) warning. The idea
 * is to get mtools compile on as many platforms as possible, but to not
 * suppress warnings if the platform is broken, as long as these warnings do
 * not prevent compilation */

#if TIME_WITH_SYS_TIME
# include <sys/time.h>
# include <time.h>
#else
# if HAVE_SYS_TIME_H
#  include <sys/time.h>
# else
#  include <time.h>
# endif
#endif

#ifndef NO_TERMIO
#ifdef HAVE_TERMIOS_H
#include <termios.h>
#endif

#ifdef HAVE_SYS_TERMIOS_H
#include <sys/termios.h>
#endif

#ifdef HAVE_TERMIO_H
#include <termio.h>
#endif


#ifdef HAVE_SYS_TERMIO_H
#include <sys/termio.h>
#endif
#endif

#ifdef HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif

#include <sys/stat.h>

#include <errno.h>
extern int errno;
extern char *sys_errlist[];
#include <pwd.h>

/* On AIX, we have to prefer strings.h, as string.h lacks a prototype 
 * for strcasecmp. On most other architectures, it's string.h which seems
 * to be more complete */
#if (defined(aix) && defined (HAVE_STRINGS_H))
# undef HAVE_STRING_H
#endif

#ifdef HAVE_STRING_H
# include <string.h>
#else
# ifdef HAVE_STRINGS_H
#  include <strings.h>
# endif
#endif

#ifdef HAVE_MEMORY_H
#include <memory.h>
#endif

#ifdef HAVE_MALLOC_H
#include <malloc.h>
#endif

#ifdef HAVE_SYS_SIGNAL_H
#include <sys/signal.h>
#endif


#ifdef HAVE_UTIME_H
#include <utime.h>
#endif

#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif

#ifdef linux
#include <linux/fd.h>
#endif


/* missing functions */
#ifndef HAVE_SRANDOM
#define srandom srand48
#endif

#ifndef HAVE_RANDOM
#define random lrand48
#endif

#ifndef HAVE_STRCHR
#define strchr index
#endif

#ifndef HAVE_STRRCHR
#define strrchr rindex
#endif


#define SIG_CAST RETSIGTYPE(*)()

#ifndef HAVE_STRDUP
extern char *strdup(__const char *str);
#endif /* HAVE_STRDUP */


#ifndef HAVE_MEMCPY
extern char *memcpy(char *s1, __const char *s2, size_t n);
#endif

#ifndef HAVE_MEMSET
extern char *memset(char *s, char c, size_t n);
#endif /* HAVE_MEMSET */


#ifndef HAVE_STRPBRK
extern char *strpbrk(__const char *string, __const char *brkset);
#endif /* HAVE_STRPBRK */


#ifndef HAVE_STRTOUL
unsigned long strtoul(const char *string, char **eptr, int base);
#endif /* HAVE_STRTOUL */

#ifndef HAVE_STRSPN
size_t strspn(__const char *s, __const char *accept);
#endif /* HAVE_STRSPN */

#ifndef HAVE_STRCSPN
size_t strcspn(__const char *s, __const char *reject);
#endif /* HAVE_STRCSPN */

#ifndef HAVE_STRERROR
char *strerror(int errno);
#endif

#ifndef HAVE_STRCASECMP
int strcasecmp(const char *, const char *);
#endif
#ifndef HAVE_STRCASECMP
int strncasecmp(const char *, const char *, int);
#endif

#ifndef linux
#undef USE_XDF
#endif

#ifdef NO_XDF
#undef USE_XDF
#endif


#ifndef __STDC__
#define signed /**/
int fflush(FILE *);

#ifdef HAVE_STRDUP
char *strdup(char *);
#endif

#ifdef HAVE_STRCASECMP
int strcasecmp(/* const char *, const char * */);
#endif
#ifdef HAVE_STRCASECMP
int strncasecmp(/* const char *, const char *, int */);
#endif
char *getenv(char *);
#ifdef HAVE_STRTOUL
unsigned long strtoul(const char *, char **, int);
#endif
int pclose(FILE *);
#ifdef HAVE_RANDOM
long int random(void);
#endif

#ifdef HAVE_SRANDOM
void srandom(unsigned int);
#endif

int atoi(char *);
FILE *fdopen(int, const char *);
FILE *popen(const char *, const char *);
#endif

#endif

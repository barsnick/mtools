/*
 * Miscellaneous routines.
 */

#include "sysincludes.h"
#include "msdos.h"
#include "stream.h"
#include "vfat.h"
#include "mtools.h"

char *get_homedir(void)
{
	struct passwd *pw;
	int uid;
	char *homedir;
	char *username;
	
	homedir = getenv ("HOME");    
	/* 
	 * first we call getlogin. 
	 * There might be several accounts sharing one uid 
	 */
	if ( homedir )
		return homedir;
	
	pw = 0;
	
	username = getenv("LOGNAME");
	if ( !username )
		username = getlogin();
	if ( username )
		pw = getpwnam( username);
  
	if ( pw == 0 ){
		/* if we can't getlogin, look up the pwent by uid */
		uid = geteuid();
		pw = getpwuid(uid);
	}
	
	/* we might still get no entry */
	if ( pw )
		return pw->pw_dir;
	return 0;
}

static FILE *tty=NULL;
static int notty=0;	
static int ttyfd=-1;
static int tty_mode = -1; /* 1 for raw, 0 for cooked, -1 for initial */
static int need_tty_reset = 0;


#ifdef TCSANOW

/* we have tcsetattr & tcgetattr. Good */
#define stty(a,b)        (void)tcsetattr(a,TCSANOW,b)
#define gtty(a,b)        (void)tcgetattr(a,b)
#ifdef TCIFLUSH
#define discard_input(a) tcflush(a,TCIFLUSH)
#else
#define discard_input(a) /**/
#endif

#else /* TCSANOW */

#ifndef TCSETS
#define TCSETS TCSETA
#define TCGETS TCGETA
#define termios termio
#endif

#define stty(a,b) (void)ioctl(a,TCSETS,(char *)b)
#define gtty(a,b) (void)ioctl(a,TCGETS,(char *)b)

#ifdef TCIFLUSH
#define discard_input(a) (void)ioctl(a,TCFLSH,TCIFLUSH)
#else
#define discard_input(a) /**/
#endif

#endif /* TCSANOW */

#define restore_tty(a) stty(STDIN,a)


#define SHFAIL	1
#define STDIN ttyfd
#define FAIL (-1)
#define DONE 0
static struct termios in_orig;

/*--------------- Signal Handler routines -------------*/

static void tty_time_out(void)
{
	int exit_code;
	signal(SIGALRM, SIG_IGN);
	if(tty && need_tty_reset)
		restore_tty (&in_orig);	
#if future
	if (fail_on_timeout)
		exit_code=SHFAIL;
	else {
		if (default_choice && mode_defined) {
			if (yes_no) {
				if ('Y' == default_choice)
					exit_code=0;
				else
					exit_code=1;
			} else
				exit_code=default_choice-minc+1;
		} else
			exit_code=DONE;
	}
#else
	exit_code = DONE;
#endif
	exit(exit_code);
}

void cleanup_and_exit(int code)
{ 
	if(tty && need_tty_reset) {
		restore_tty (&in_orig);
		signal (SIGHUP, SIG_IGN);
		signal (SIGINT, SIG_IGN);
		signal (SIGTERM, SIG_IGN);
		signal (SIGALRM, SIG_IGN);
	}
	exit(code);
}

static void clean_up_and_fail(void)
{
	cleanup_and_exit(SHFAIL);
}

static void set_raw_tty(int mode)
{
	struct termios in_raw;

	if(mode != tty_mode && mode != -1) {
		/* Determine existing TTY settings */
		gtty (STDIN, &in_orig);
		need_tty_reset = 1;

		/*------ Restore original TTY settings on exit --------*/
		signal (SIGHUP, (SIG_CAST)clean_up_and_fail);
		signal (SIGINT, (SIG_CAST)clean_up_and_fail);
		signal (SIGTERM, (SIG_CAST)clean_up_and_fail);
		signal (SIGALRM, (SIG_CAST)tty_time_out);
	
		/* Change STDIN settings to raw */

		gtty (STDIN, &in_raw);
		if(mode) {
			in_raw.c_lflag &= ~ICANON;
			in_raw.c_cc[VMIN]=1;
			in_raw.c_cc[VTIME]=0;
			stty (STDIN, &in_raw);
		} else {
			in_raw.c_lflag |= ICANON;
			stty (STDIN, &in_raw);
		}
		tty_mode = mode;
		discard_input(STDIN);
	}
}

FILE *opentty(int mode)
{
	if(notty)
		return NULL;

	if (tty == NULL) {
		ttyfd = open("/dev/tty", O_RDONLY);
		if(ttyfd >= 0) {
			tty = fdopen(ttyfd, "r");
		}
	}

	if  (tty == NULL){
		if ( !isatty(0) ){
			notty = 1;
			return NULL;
		}
		ttyfd = 0;
		tty = stdin;
	}

	set_raw_tty(mode);
	return tty;
}

int ask_confirmation(char *format, char *p1, char *p2)
{
	char ans[10];

	if(!opentty(-1))
		return 0;

	while (1) {
		fprintf(stderr, format, p1, p2);
		fflush(stderr);
		fflush(opentty(-1));
		if (mtools_raw_tty) {
			ans[0] = fgetc(opentty(1));
			fputs("\n", stderr);
		} else {
			fgets(ans,9, opentty(0));
		}
		if (ans[0] == 'y' || ans[0] == 'Y')
			return 0;
		if (ans[0] == 'n' || ans[0] == 'N')
			return -1;
	}
}


FILE *open_mcwd(char *mode)
{
	struct stat sbuf;
	char file[EXPAND_BUF];
	long now;
	char *mcwd_path;
	char *homedir;

	mcwd_path = getenv("MCWD");
	if (mcwd_path == NULL || *mcwd_path == '\0'){
		homedir= get_homedir();
		if ( homedir ){
			strncpy(file, homedir, MAXPATHLEN);
			file[MAXPATHLEN]='\0';
			strcat( file, "/.mcwd");
		} else
			strcpy(file,"/tmp/.mcwd");
	} else
		expand(mcwd_path,file);

	if (*mode == 'r'){
		if (stat(file, &sbuf) < 0)
			return NULL;
		/*
		 * Ignore the info, if the file is more than 6 hours old
		 */
		time(&now);
		if (now - sbuf.st_mtime > 6 * 60 * 60) {
			fprintf(stderr,
				"Warning: \"%s\" is out of date, removing it\n",
				file);
			unlink(file);
			return NULL;
		}
	}
	
	return  fopen(file, mode);
}
	

/* Fix the info in the MCWD file to be a proper directory name.
 * Always has a leading separator.  Never has a trailing separator
 * (unless it is the path itself).  */

char *fix_mcwd(char *ans)
{
	FILE *fp;
	char *s;
	char buf[BUFSIZ];

	fp = open_mcwd("r");
	if(!fp){
		strcpy(ans, "A:/");
		return ans;
	}

	if (!fgets(buf, BUFSIZ, fp))
		return("A:/");

	buf[strlen(buf) -1] = '\0';
	fclose(fp);
					/* drive letter present? */
	s = buf;
	if (buf[0] && buf[1] == ':') {
		strncpy(ans, buf, 2);
		ans[2] = '\0';
		s = &buf[2];
	} else 
		strcpy(ans, "A:");
					/* add a leading separator */
	if (*s != '/' && *s != '\\') {
		strcat(ans, "/");
		strcat(ans, s);
	} else
		strcat(ans, s);
					/* translate to upper case */
	for (s = ans; *s; ++s) {
		*s = toupper(*s);
		if (*s == '\\')
			*s = '/';
	}
					/* if only drive, colon, & separator */
	if (strlen(ans) == 3)
		return(ans);
					/* zap the trailing separator */
	if (*--s == '/')
		*s = '\0';
	return ans;
}

int maximize(int *target, int max)
{
	if (*target > max)
		*target = max;
	return *target;
}

int minimize(int *target, int min)
{
	if (*target < min)
		*target = min;
	return *target;
}


void *safe_malloc(size_t size)
{
	void *p;

	p = malloc(size);
	if(!p){
		fprintf(stderr,"Out of memory error\n");
		exit(1);
	}
	return p;
}

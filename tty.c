#include "sysincludes.h"
#include "mtools.h"

static FILE *tty=NULL;
static int notty=0;	
static int ttyfd=-1;
static int tty_mode = -1; /* 1 for raw, 0 for cooked, -1 for initial */
static int need_tty_reset = 0;
static int handlerIsSet = 0;
int	mtools_raw_tty = 1;


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

static void cleanup_tty(void)
{ 
	if(tty && need_tty_reset) {
		restore_tty (&in_orig);
		signal (SIGHUP, SIG_IGN);
		signal (SIGINT, SIG_IGN);
		signal (SIGTERM, SIG_IGN);
		signal (SIGALRM, SIG_IGN);
	}
}

static void fail(void)
{
	exit(1);
}

static void set_raw_tty(int mode)
{
	struct termios in_raw;

	if(mode != tty_mode && mode != -1) {
		/* Determine existing TTY settings */
		gtty (STDIN, &in_orig);
		need_tty_reset = 1;
		if(!handlerIsSet) {
			atexit(cleanup_tty);
			handlerIsSet = 1;
		}

		/*------ Restore original TTY settings on exit --------*/
		signal (SIGHUP, (SIG_CAST) fail);
		signal (SIGINT, (SIG_CAST) fail);
		signal (SIGTERM, (SIG_CAST) fail);
		signal (SIGALRM, (SIG_CAST) tty_time_out);
	
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
/* BeOS does not have a working /dev/tty, so we use stdin */
#ifndef __BEOS__
	if (tty == NULL) {
		ttyfd = open("/dev/tty", O_RDONLY);
		if(ttyfd >= 0) {
			tty = fdopen(ttyfd, "r");
		}
	}
#endif
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

int ask_confirmation(const char *format, const char *p1, const char *p2)
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

/*
 * mmd.c
 * Makes an MSDOS directory
 */


#define LOWERCASE

#include "sysincludes.h"
#include "msdos.h"
#include "mtools.h"
#include "vfat.h"
#include "streamcache.h"
#include "plain_io.h"
#include "nameclash.h"
#include "file.h"
#include "fs.h"

/*
 * Preserve the file modification times after the fclose()
 */

typedef struct Arg_t {
	char *target;
	int nowarn;
	int interactive;
	int verbose;
	StreamCache_t sc;

	Stream_t *SrcDir;
	int entry;
	ClashHandling_t ch;
	Stream_t *targetDir;
	Stream_t *targetFs;
	char *progname;
} Arg_t;


/*
 * Open the named file for read, create the cluster chain, return the
 * directory structure or NULL on error.
 */
static struct directory *makeeit(char *dosname,
				 char *longname,
				 void *arg0,
				 struct directory *dir)
{
	Stream_t *Target;
	long now;
	Arg_t *arg = (Arg_t *) arg0;
	int fat;

	/* will it fit? At least one sector must be free */
	if (! getfree(arg->targetFs)) {
		fprintf(stderr,"Disk full\n");
		return NULL;
	}
	
	/* find out current time */
	time(&now);
	mk_entry(".", 0x10, 1, 0, now, dir);
	Target = open_file(COPY(arg->targetFs), dir);
	if(!Target){
		FREE(&arg->targetFs);
		fprintf(stderr,"Could not open Target\n");
		return NULL;
	}

	dir_grow(Target, arg->targetFs, 0);
	/* this allocates the first cluster for our directory */

	GET_DATA(arg->targetDir, 0, 0, 0, &fat);
	mk_entry("..         ", 0x10, fat, 0, now, dir);
	dir_write(Target, 1, dir);

	GET_DATA(Target, 0, 0, 0, &fat);
	mk_entry(".          ", 0x10, fat, 0, now, dir);
	dir_write(Target, 0, dir);

	mk_entry(dosname, 0x10, fat, 0, now, dir);
	FREE(&Target);
	return dir;
}


void mmd(int argc, char **argv, int type)
{
	int ret = 0;
	Stream_t *Dir, *Fs;
	Arg_t arg;
	int c, oops;
	char filename[VBUFSIZE];
	int i;

	arg.progname = argv[0];

	/* get command line options */

	init_clash_handling(& arg.ch);

	oops = 0;

	/* get command line options */
	arg.nowarn = 0;
	arg.interactive = 0;
	while ((c = getopt(argc, argv, "invorsamORSAM")) != EOF) {
		switch (c) {
			case 'i':
				arg.interactive = 1;
				break;
			case 'n':
				arg.nowarn = 1;
				break;
			case 'v':
				arg.verbose = 1;
				break;
			case '?':
				oops = 1;
				break;
			default:
				if(handle_clash_options(&arg.ch, c))
					oops=1;
				break;
		}
	}

	if (oops || (argc - optind) < 1) {
		fprintf(stderr,
			"Mtools version %s, dated %s\n", mversion, mdate);
		fprintf(stderr,
			"Usage: %s [-itnmvV] file targetfile\n", argv[0]);
		fprintf(stderr,
			"       %s [-itnmvV] file [files...] target_directory\n", 
			argv[0]);
		cleanup_and_exit(1);
	}


	init_sc(&arg.sc);		
	arg.sc.arg = (void *) &arg;
	arg.sc.openflags = O_RDWR;
	ret = 0;

	for(i=optind; i < argc; i++){
		if(got_signal)
			break;
		Dir = open_subdir(&arg.sc, argv[i], O_RDWR, &Fs);
		if(!Dir){
			fprintf(stderr,"Could not open parent directory\n");
			cleanup_and_exit(1);
		}
		arg.targetDir = Dir;
		arg.targetFs = Fs;
		get_name(argv[i], filename, arg.sc.mcwd);
		if(!filename[0]){
			fprintf(stderr,"Empty filename\n");
			ret |= MISSED_ONE;
			FREE(&Dir);
			continue;
		}
		if(mwrite_one(Dir, Fs, argv[0], filename,0,
			      makeeit, (void *)&arg, &arg.ch)<=0){
			fprintf(stderr,"Could not create %s\n", argv[i]);
			ret |= MISSED_ONE;
		} else
			ret |= GOT_ONE;
		FREE(&Dir);
	}

	finish_sc(&arg.sc);

	if ((ret & GOT_ONE) && ( ret & MISSED_ONE))
		cleanup_and_exit(2);
	if (ret & MISSED_ONE)
		cleanup_and_exit(1);
	cleanup_and_exit(0);
}

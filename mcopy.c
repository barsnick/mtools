/*
 * mcopy.c
 * Copy an MSDOS files to and from Unix
 *
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

static void set_mtime(char *target, long mtime)
{
#ifdef HAVE_UTIMES
	struct timeval tv[2];

	if (mtime != 0L) {
		tv[0].tv_sec = mtime;
		tv[0].tv_usec = 0;
		tv[1].tv_sec = mtime;
		tv[1].tv_usec = 0;
		utimes(target, tv);
	}
#else
#ifdef HAVE_UTIME
	struct utimbuf utbuf;

	if (mtime != 0L) {
		utbuf.actime = mtime;
		utbuf.modtime = mtime;
		utime(target, &utbuf);
	}
#endif
#endif
	return;
}

typedef struct Arg_t {
	int preserve;
	char *target;
	int textmode;
	int needfilter;
	int nowarn;
	int single;
	int interactive;
	int verbose;
	int type;
	StreamCache_t sc;
	ClashHandling_t ch;
	Stream_t *sourcefile;
	Stream_t *targetDir;
	Stream_t *targetFs;
	char *progname;
} Arg_t;

static int read_file(Stream_t *Dir, StreamCache_t *sc, int entry, 
		     int needfilter)
{
	Arg_t *arg=(Arg_t *) sc->arg;
	long mtime;
	Stream_t *File=sc->File;
	Stream_t *Target, *Source;
	struct stat stbuf;
	int ret;
	char errmsg[80];

	File->Class->get_data(File, &mtime, 0, 0, 0);
	
	if (!arg->preserve)
		mtime = 0L;

	/* if we are creating a file, check whether it already exists */
	if (!arg->nowarn && strcmp(arg->target, "-") && 
	    !access(arg->target, 0)){
		if( ask_confirmation("File \"%s\" exists, overwrite (y/n) ? ",
				     arg->target,0))
			return MISSED_ONE;
		
		/* sanity checking */
		if (!stat(arg->target, &stbuf) && !S_ISREG(stbuf.st_mode)) {
			fprintf(stderr,"\"%s\" is not a regular file\n",
				arg->target);			
			return MISSED_ONE;
		}
	}

	if(!arg->type)
		fprintf(stderr,"Copying %s\n", sc->outname);
	if(got_signal)
		return MISSED_ONE;
	if ((Target = SimpleFloppyOpen(0, 0, arg->target,
				       O_WRONLY | O_CREAT | O_TRUNC,
				       errmsg))) {
		ret = 0;
		if(needfilter && arg->textmode){
			Source = open_filter(COPY(File));
			if (!Source)
				ret = -1;
		} else
			Source = COPY(File);

		if (ret == 0 )
			ret = copyfile(Source, Target);
		FREE(&Source);
		FREE(&Target);
		if(ret < -1){
			unlink(arg->target);
			return MISSED_ONE;
		}
		if(strcmp(arg->target,"-"))
			set_mtime(arg->target, mtime);
		return GOT_ONE;
	} else {
		fprintf(stderr,"%s\n", errmsg);
		return MISSED_ONE;
	}
}

static  int dos_read(Stream_t *Dir, StreamCache_t *sc, int entry)
{
	return read_file(Dir, sc, entry, 1);
}


static  int unix_read(char *name, StreamCache_t *sc)
{
	return read_file(0, sc, -1, 0);
}


/*
 * Open the named file for read, create the cluster chain, return the
 * directory structure or NULL on error.
 */
static struct directory *writeit(char *dosname,
				 char *longname,
				 void *arg0,
				 struct directory *dir)
{
	Stream_t *Target;
	long now;
	int type, fat, ret;
	long date;
	unsigned long filesize, newsize;
	Arg_t *arg = (Arg_t *) arg0;

	if (arg->sc.File->Class->get_data(arg->sc.File,
					  & date, &filesize, &type, 0) < 0 ){
		fprintf(stderr, "Can't stat source file\n");
		return NULL;
	}

	if (type){
		if (arg->verbose)
			fprintf(stderr, "\"%s\" is a directory\n", longname);
		return NULL;
	}

	if (!arg->single)
		fprintf(stderr,"Copying %s\n", longname);
	if(got_signal)
		return NULL;

	/* will it fit? */
	if (filesize > getfree(arg->targetFs)) {
		fprintf(stderr,"Disk full\n");
		return NULL;
	}
	
	/* preserve mod time? */
	if (arg->preserve)
		now = date;
	else
		time(&now);

	mk_entry(dosname, 0x20, 0, 0, now, dir);
	Target = open_file(COPY(arg->targetFs), dir);
	if(!Target){
		fprintf(stderr,"Could not open Target\n");
		cleanup_and_exit(1);
	}
	if (arg->needfilter & arg->textmode)
		Target = open_filter(Target);
	ret = copyfile(arg->sc.File, Target);
	GET_DATA(Target, 0, &newsize, 0, &fat);
	FREE(&Target);
	if(ret < 0 ){
		fat_free(arg->targetFs, fat);
		return NULL;
	} else {
		mk_entry(dosname, 0x20, fat, newsize, now, dir);
		return dir;
	}
}


static int write_file(Stream_t *Dir, StreamCache_t *sc, int entry,
		      int needfilter)
/* write a messy dos file to another messy dos file */
{
	int result;
	Arg_t * arg = (Arg_t *) (sc->arg);

	arg->needfilter = needfilter;
	if (arg->targetDir == Dir){
		arg->ch.ignore_entry = -1;
		arg->ch.source = entry;
	} else {
		arg->ch.ignore_entry = -1;
		arg->ch.source = -2;
	}
	result = mwrite_one(arg->targetDir, arg->targetFs,
			    arg->progname, arg->target, 0,
			    writeit, (void *)arg, &arg->ch);
	if(result == 1)
		return GOT_ONE;
	else
		return MISSED_ONE;
}


static int dos_write(Stream_t *Dir, StreamCache_t *sc, int entry)
{
	return write_file(Dir, sc, entry, 0);
}

static int unix_write(char *name, StreamCache_t *sc)
/* write a Unix file to a messy DOS fs */
{
	return write_file(0, sc, 0, 1);
}


void mcopy(int argc, char **argv, int mtype)
{
	Stream_t *SubDir,*Dir, *Fs;
	int have_target;
	Arg_t arg;
	int c, oops, ret;
	char filename[VBUFSIZE];
	
	struct stat stbuf;

	int interactive;

	arg.progname = argv[0];

	/* get command line options */

	init_clash_handling(& arg.ch);

	oops = interactive = 0;


	/* get command line options */
	arg.preserve = 0;
	arg.nowarn = 0;
	arg.textmode = 0;
	arg.interactive = 0;
	arg.verbose = 0;
	arg.type = mtype;
	while ((c = getopt(argc, argv, "itnmvorsamORSAM")) != EOF) {
		switch (c) {
			case 'i':
				arg.interactive = 1;
				break;
			case 't':
				arg.textmode = 1;
				break;
			case 'n':
				arg.nowarn = 1;
				break;
			case 'm':
				arg.preserve = 1;
				break;
			case 'v':	/* dummy option for mcopy */
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
			"Usage: %s [-itnmvV] sourcefile targetfile\n", argv[0]);
		fprintf(stderr,
			"       %s [-itnmvV] sourcefile [sourcefiles...] targetdirectory\n", 
			argv[0]);
		cleanup_and_exit(1);
	}

	/* only 1 file to copy... */
	arg.single = SINGLE;
	
	init_sc(&arg.sc);		
	arg.sc.arg = (void *) &arg;
	arg.sc.openflags = O_RDONLY;

	arg.targetDir = NULL;
	if(mtype){

		/* Mtype = copying to stdout */

		have_target = 0;
		arg.target = "-";
		arg.sc.outname = filename;
		arg.single = 0;		
		arg.sc.callback = dos_read;
		arg.sc.unixcallback = unix_read;
	} else if(argc - optind >= 2 && 
		  argv[argc-1][0] && argv[argc-1][1] == ':'){

		/* Copying to Dos */

		have_target = 1;
		Dir = open_subdir(&arg.sc, argv[argc-1], O_RDWR, &Fs);
		arg.targetFs = Fs;
		if(!Dir){
			fprintf(stderr,"Bad target\n");
			cleanup_and_exit(1);
		}
		get_name(argv[argc-1], filename, arg.sc.mcwd);
		SubDir = descend(Dir, Fs, filename, 0, 0);
		if (!SubDir){
			/* the last parameter is a file */
			if ( argc - optind != 2 ){
				fprintf(stderr,
					"%s: Too many arguments, or destination directory omitted\n", argv[0]);
				cleanup_and_exit(1);
			}
			arg.targetDir = Dir;
			arg.sc.outname = 0; /* toss away source name */
			arg.target = filename; /* store near name given as
						* target */
			arg.single = 1;
		} else {
			arg.targetDir = SubDir;
			FREE(&Dir);
			arg.sc.outname = arg.target = filename;
			arg.single = 0;
		}
		arg.sc.callback = dos_write;
		arg.sc.unixcallback = unix_write;
	} else {

		/* Copying to Unix */

		have_target = 1;
		if (argc - optind == 1){
			/* one argument: copying to current directory */
			arg.single = 0;
			have_target = 0;
			arg.target = arg.sc.outname = filename;
		} else if (strcmp(argv[argc-1],"-") && 
			   !stat(argv[argc-1], &stbuf) &&
			   S_ISDIR(stbuf.st_mode)){
			arg.target = (char *) safe_malloc(VBUFSIZE + 1 + 
							  strlen(argv[argc-1]));
			strcpy(arg.target, argv[argc-1]);
			strcat(arg.target,"/");
			arg.sc.outname = arg.target+strlen(arg.target);
		} else if (argc - optind != 2) {
			/* last argument is not a directory: we should only
			 * have one source file */
			fprintf(stderr,
				"%s: Too many arguments or destination directory omitted\n", 
				argv[0]);
			cleanup_and_exit(1);
		} else {
			arg.target = argv[argc-1];
			arg.sc.outname = filename;
		}

		arg.sc.callback = dos_read;
		arg.sc.unixcallback = unix_read;
	}

	arg.sc.lookupflags = ACCEPT_PLAIN | DO_OPEN | arg.single;
	ret=main_loop(&arg.sc, argv[0], argv + optind, 
		      argc - optind - have_target);
	FREE(&arg.targetDir);
	finish_sc(&arg.sc);

	cleanup_and_exit(ret);
}

/*
 * mmove.c
 * Renames/moves an MSDOS file
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

typedef struct Arg_t {
	char *target;
	char *fromname;
	int nowarn;
	int single;
	int interactive;
	int verbose;
	int oldsyntax;
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
static struct directory *renameit(char *dosname,
				  char *longname,
				  void *arg0,
				  struct directory *dir)
{
	int entry;
	struct directory parentdir;
	Stream_t *Dir;
	Arg_t *arg = (Arg_t *) arg0;
	int fat;


	*dir = arg->sc.dir;
	strncpy(dir->name, dosname, 8);
	strncpy(dir->ext, dosname + 8, 3);

	if( (dir->attr & 0x10) && arg->targetDir){
		/* we have a directory here. Change its parent link */

		Dir = open_file( COPY(arg->targetFs), & arg->sc.dir);
		entry = 0;
		if(vfat_lookup(Dir, arg->targetFs,
			       &parentdir, &entry, 0, "..", ACCEPT_DIR,
			       NULL, NULL, NULL, NULL))
			fprintf(stderr," Directory has no parent entry\n");
		else {
			entry--; /* rewind entry */
			GET_DATA(arg->targetDir, 0, 0, 0, &fat);
			parentdir.start[1] = (fat >> 8) & 0xff;
			parentdir.start[0] = fat & 0xff;
			dir_write(Dir, entry, &parentdir);
			if(arg->verbose){
				fprintf(stderr,
					"Easy, isn't it? I wonder why DOS can't do this.\n");
			}
		}
		FREE(&Dir);
	}

	/* wipe out original entry */
	arg->sc.dir.name[0] = DELMARK;
	dir_write(arg->SrcDir, arg->entry, &arg->sc.dir);

	return dir;
}


static int rename_file(Stream_t *Dir, StreamCache_t *sc, int entry)
/* write a messy DOS file to another messy DOS file */
{
	int result;
	Stream_t *targetDir;
	char *shortname, *longname;

	Arg_t * arg = (Arg_t *) (sc->arg);

	arg->SrcDir = Dir;
	arg->entry = entry;
	if(arg->oldsyntax){
		targetDir = Dir;
		arg->targetFs = sc->Fs;
	} else
		targetDir = arg->targetDir;

	if (targetDir == Dir){
		arg->ch.ignore_entry = entry;
		arg->ch.source = entry;
	} else {
		arg->ch.ignore_entry = -1;
		arg->ch.source = -2;
	}


	if(arg->oldsyntax && !strcasecmp(sc->shortname, arg->fromname)){
		longname = sc->longname;
		shortname = arg->target;
	} else {
		longname = arg->target;
		shortname = 0;
	}

	result = mwrite_one(targetDir, arg->targetFs,
			    arg->progname, longname, shortname,
			    renameit, (void *)arg, &arg->ch);
	if(result == 1)
		return GOT_ONE;
	else
		return MISSED_ONE;
}


void mmove(int argc, char **argv, int oldsyntax)
{
	Stream_t *SubDir,*Dir, *Fs;
	Arg_t arg;
	int c, oops, ret;
	char filename[VBUFSIZE];
	char shortname[13];
	char longname[VBUFSIZE];
	char def_drive;
	char *s;
	int i;
	int interactive;

	arg.progname = argv[0];

	/* get command line options */

	init_clash_handling(& arg.ch);

	oops = interactive = 0;


	/* get command line options */
	arg.verbose = 0;
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

	if (oops || (argc - optind) < 2) {
		fprintf(stderr,
			"Mtools version %s, dated %s\n", mversion, mdate);
		fprintf(stderr,
			"Usage: %s [-itnmvV] file targetfile\n", argv[0]);
		fprintf(stderr,
			"       %s [-itnmvV] file [files...] target_directory\n", 
			argv[0]);
		cleanup_and_exit(1);
	}


	/* only 1 file to rename... */
	arg.single = SINGLE;
	
	init_sc(&arg.sc);		
	arg.sc.arg = (void *) &arg;
	arg.sc.openflags = O_RDWR;

	/* look for a default drive */
	def_drive = '\0';
	for(i=optind; i<argc; i++)
		if(argv[i][0] && argv[i][1] == ':' ){
			if(!def_drive)
				def_drive = toupper(argv[i][0]);
			else if(def_drive != toupper(argv[i][0])){
				fprintf(stderr,
					"Cannot move files across different drives\n");
				cleanup_and_exit(1);
			}
		}

	if(def_drive)
		*(arg.sc.mcwd) = def_drive;

	if (oldsyntax && (argc - optind != 2 || strpbrk(":/", argv[argc-1])))
		oldsyntax = 0;

	arg.oldsyntax = oldsyntax;

	if (!oldsyntax){
		Dir = open_subdir(&arg.sc, argv[argc-1], O_RDWR, &Fs);
		arg.targetFs = Fs;
		if(!Dir){
			fprintf(stderr,"Bad target\n");
			cleanup_and_exit(1);
		}
		get_name(argv[argc-1], filename, arg.sc.mcwd);
		SubDir = descend(Dir, Fs, filename, 0, 0);
		if (!SubDir){
			/* the last parameter is not a directory: take
			 * it as the new name */
			if ( argc - optind != 2 ){
				fprintf(stderr,
					"%s: Too many arguments, or destination directory omitted\n", 
					argv[0]);
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
	} else {
		arg.fromname = argv[optind];
		if(arg.fromname[0] && arg.fromname[1] == ':')
			arg.fromname += 2;
		s = strrchr(arg.fromname, '/');
		if(s)
			arg.fromname = s+1;
		arg.sc.outname = filename;
		arg.single = 0;
		arg.targetDir = NULL;
		arg.target = argv[argc-1];
	}

	arg.sc.callback = rename_file;

	arg.sc.lookupflags = ACCEPT_PLAIN | ACCEPT_DIR | DO_OPEN | arg.single;

	arg.sc.longname = longname;
	arg.sc.shortname = shortname;

	ret=main_loop(&arg.sc, argv[0], argv + optind, argc - optind - 1);
	FREE(&arg.targetDir);
	finish_sc(&arg.sc);

	cleanup_and_exit(ret);
}

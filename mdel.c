/*
 * mdel.c
 * Delete an MSDOS file
 *
 */

#include "sysincludes.h"
#include "msdos.h"
#include "mtools.h"
#include "stream.h"
#include "streamcache.h"
#include "fs.h"
#include "file.h"

typedef struct Arg_t {
	int deltype;
	int verbose;
	char *progname;
} Arg_t;

static int del_entry(Stream_t *Dir, StreamCache_t *sc, int entry)
{
	Arg_t *arg=(Arg_t *) sc->arg;
	unsigned int start;

	if (arg->verbose)
		fprintf(stderr,"Removing %s\n", sc->outname);
	if(got_signal)
		return MISSED_ONE;

	if ((sc->dir.attr & 0x05) &&
	    (ask_confirmation("%s: \"%s\" is read only, erase anyway (y/n) ? ",
			      arg->progname, sc->outname)))
		return MISSED_ONE;
	start = sc->dir.start[1] * 0x100 + sc->dir.start[0];
	if (fat_free(sc->Fs,start))
		return MISSED_ONE;
	sc->dir.name[0] = (char) 0xe5;
	dir_write(Dir,entry,& sc->dir);
	return GOT_ONE;
}

static int del_file(Stream_t *Dir, StreamCache_t *sc, int entry)
{
	char shortname[13];
	int sub_entry = 0;
	Stream_t *SubDir;
	Arg_t *arg = (Arg_t *) sc->arg;
	StreamCache_t sonsc;
	char outname[VBUFSIZE];
	int ret;

	sonsc = *sc;
	sonsc.outname = outname;
	sonsc.arg = sc->arg;

	if (sc->dir.attr & 0x10){
		/* a directory */

		SubDir = open_file( COPY(sc->Fs), & sc->dir);

		ret = 0;
		while(!vfat_lookup(SubDir, sc->Fs, &sonsc.dir, &sub_entry, 
				   0, "*",
				   ACCEPT_DIR | ACCEPT_PLAIN,
				   outname, shortname, NULL, NULL) ){
			if((unsigned char)shortname[0] != 0xe5 &&
			   shortname[0] &&
			   shortname[0] != '.' ){
				if(arg->deltype != 2){
					fprintf(stderr,
						"Directory %s non empty\n", 
						sc->outname);
					ret = MISSED_ONE;
					break;
				}
				if(got_signal) {
					ret = MISSED_ONE;
					break;
				}
				ret = del_file(SubDir, &sonsc, sub_entry - 1);
				if( ret & MISSED_ONE)
					break;
				ret = 0;
			}
		}
		FREE(&SubDir);
		if(ret)
			return ret;
	}
	return del_entry(Dir, sc, entry);
}

void mdel(int argc, char **argv, int deltype)
{
	int fargn;
	Arg_t arg;
	StreamCache_t sc;
	int ret;
	char filename[VBUFSIZE];

	if (argc > 1 && !strcmp(argv[1], "-v")) {
		arg.verbose = 1;
		fargn = 2;
	} else {
		arg.verbose = 0;
		fargn = 1;
	}
	if (argc < 2 || (argv[1][0] == '-' && !arg.verbose)) {
		fprintf(stderr, "Mtools version %s, dated %s\n", mversion, mdate);
		fprintf(stderr, "Usage: %s [-vV] msdosfile [msdosfiles...]\n", argv[0]);
		cleanup_and_exit(1);
	}

	init_sc(&sc);
	sc.callback = del_file;
	sc.outname = filename;
	sc.arg = (void *) &arg;
	sc.openflags = O_RDWR;
	arg.deltype = deltype;
	switch(deltype){
	case 0:
		sc.lookupflags = ACCEPT_PLAIN; /* mdel */
		break;
	case 1:
		sc.lookupflags = ACCEPT_DIR; /* mrd */
		break;
	case 2:
		sc.lookupflags = ACCEPT_DIR | ACCEPT_PLAIN; /* mdeltree */
		break;
	}
	arg.progname = argv[0];
	ret=main_loop(&sc, argv[0], argv + fargn, argc - fargn);
	finish_sc(&sc);
	cleanup_and_exit(ret);
}

/*
 * mattrib.c
 * Change MSDOS file attribute flags
 */

#include "sysincludes.h"
#include "msdos.h"
#include "mtools.h"
#include "streamcache.h"

typedef struct Arg_t {
	char add;
	unsigned char remove;
	struct StreamCache_t sc;
} Arg_t;

static int attrib_file(Stream_t *Dir, StreamCache_t *sc, int entry)
{
	Arg_t *arg=(Arg_t *) sc->arg;

	sc->dir.attr = (sc->dir.attr & arg->remove) | arg->add;
	dir_write(Dir,entry,& sc->dir);
	return GOT_ONE;
}


static int view_attrib(Stream_t *Dir, StreamCache_t *sc, int entry)
{
	printf("  ");
	if(sc->dir.attr & 0x20)
		putchar('A');
	else
		putchar(' ');
	fputs("  ",stdout);
	if(sc->dir.attr & 0x4)
		putchar('S');
	else
		putchar(' ');
	if(sc->dir.attr & 0x2)
		putchar('H');
	else
		putchar(' ');
	if(sc->dir.attr & 0x1)
		putchar('R');
	else
		putchar(' ');
	printf("     %c:%s", sc->drivename, sc->pathname);
	if(strlen(sc->pathname) != 1 )
		putchar('/');	
	puts(sc->outname);
	return 3;
}

void mattrib(int argc, char **argv, int type)
{
	Arg_t arg;
	enum action_type { LEAVE, ADD, REMOVE} action;
	int oops, fargn, view;
	char code = 0;
	int i,ret;
	char filename[VBUFSIZE];
	char pathname[MAX_PATH];

	oops = 0;
	fargn = 1;
	arg.add = 0;
	arg.remove = 0xff;
	view = 1;

	/* can't use getopt(3)... */
	for (i = 1; i < argc; i++) {
		action = LEAVE;  /* avoid compiler warnings */
		switch (argv[i][0]) {
			case '-':
				action = REMOVE;
				break;
			case '+':
				action = ADD;
				break;
			default:
				fargn = i;
				goto break2;
		}
		switch (toupper(argv[i][1])) {
		case 'A':
			code = 0x20;
			break;
		case 'H':
			code = 0x2;
			break;
		case 'R':
			code = 0x1;
			break;
		case 'S':
			code = 0x4;
			break;
		case'?':
			oops++;
			break;
		}
		if (oops)
			break;
		view = 0;
		switch(action){
		case ADD:
			arg.add |= code;
			break;
		case REMOVE:
			arg.remove &= ~code;
			break;
		case LEAVE:
			break;
		}			
	}

 break2:
	if (argc <= fargn || argv[fargn][0] == '\0' || oops) {
		fprintf(stderr, "Mtools version %s, dated %s\n", mversion, mdate);
		fprintf(stderr, "Usage: %s [-V] [-a|+a] [-h|+h] [-r|+r] [-s|+s] msdosfile [msdosfiles...]\n", argv[0]);
		cleanup_and_exit(1);
	}

	init_sc(&arg.sc);
	if(view){
		arg.sc.callback = view_attrib;
		arg.sc.openflags = O_RDONLY;
	} else {
		arg.sc.callback = attrib_file;
		arg.sc.openflags = O_RDWR;
	}

	arg.sc.outname = filename;
	arg.sc.arg = (void *) &arg;
	arg.sc.lookupflags = ACCEPT_PLAIN | ACCEPT_DIR;
	arg.sc.pathname = pathname;
	ret=main_loop(&arg.sc, argv[0], argv + fargn, argc - fargn);
	finish_sc(&arg.sc);
	cleanup_and_exit(ret);
}

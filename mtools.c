#include "sysincludes.h"
#include "msdos.h"
#include "mtools.h"
#include "patchlevel.h"

const char *mversion = VERSION;
const char *mdate = DATE;
int	mtools_raw_tty = 1;

#define DISPATCH(cmd,fn,type) if (!strcmp(name,cmd)) fn(argc, argv, type)


void main(int argc,char **argv)
{
	char *name;

	/* print the version */
	if(argc >= 2 && strcmp(argv[1], "-V") == 0) {
		printf("Mtools version %s, dated %s\n", mversion, mdate);
		exit(0);
	}

	if ((name = strrchr(argv[0],'/')))
		name++;
	else name = argv[0];
	argv[0] = name;
	
	read_config();
	setup_signal();
	
	DISPATCH("mattrib",mattrib, 0);
	DISPATCH("mbadblocks",mbadblocks, 0);
	DISPATCH("mcd",mcd, 0);
	DISPATCH("mcopy",mcopy, 0);
	DISPATCH("mdel",mdel, 0);
	DISPATCH("mdeltree",mdel, 2);
	DISPATCH("mdir",mdir, 0);
	DISPATCH("mformat",mformat, 0);
	DISPATCH("mlabel",mlabel, 0);
	DISPATCH("mmd",mmd, 0);
	DISPATCH("mmount",mmount, 0);
	DISPATCH("mrd",mdel, 1);
	DISPATCH("mread",mcopy, 0);
	DISPATCH("mmove",mmove, 0);
	DISPATCH("mren",mmove, 1);
	DISPATCH("mtest", mtest, 0);
	DISPATCH("mtype",mcopy, 1);
	DISPATCH("mwrite",mcopy, 0);
	fprintf(stderr,"Unknown mtools command '%s'\n",name);

	cleanup_and_exit(1);
}

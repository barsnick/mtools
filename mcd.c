/*
 * mcd.c: Change MSDOS directories
 */

#include "sysincludes.h"
#include "msdos.h"
#include "streamcache.h"
#include "mtools.h"

void mcd(int argc, char **argv, int type)
{
	Stream_t *Dir, *SubDir, *Fs;
	FILE *fp;
	char pathname[MAX_PATH], filename[VBUFSIZE];
	struct StreamCache_t sc;

	if (argc > 2) {
		fprintf(stderr, "Mtools version %s, dated %s\n", mversion, mdate);
		fprintf(stderr, "Usage: %s: [-V] msdosdirectory\n", argv[0]);
		cleanup_and_exit(1);
	}

	init_sc(&sc);
	if (argc == 1) {
		printf("%s\n", sc.mcwd);
		cleanup_and_exit(0);
	}
	sc.pathname = pathname;
	Dir = open_subdir(&sc, argv[1], O_RDONLY, &Fs);
	if(!Dir){
		fprintf(stderr,"No such directory\n");
		cleanup_and_exit(1);
	}

	SubDir = descend(Dir, Fs, sc.filename, 0, filename);
	if(!SubDir){
		fprintf(stderr,"No such directory\n");
		cleanup_and_exit(1);
	}
	if(strlen(pathname) > 1)
		strcat(pathname,"/");
	strcat(pathname, filename);

	FREE(&Dir);
	FREE(&SubDir);
	finish_sc(&sc);

	if (!(fp = open_mcwd("w"))){
		fprintf(stderr,"%s: Can't open mcwd file for write\n",argv[0]);
		cleanup_and_exit(1);
	}
	fprintf(fp, "%c:%s\n", sc.drivename, pathname);
	fclose(fp);
	cleanup_and_exit(0);
}

/*
 * Mount an MSDOS disk
 *
 * written by:
 *
 * Alain L. Knaff			
 * Alain.Knaff@inrialpes.fr
 *
 */

#include "sysincludes.h"
#include "msdos.h"
#include "mtools.h"

#ifdef linux
#include "patchlevel.h"
#include <linux/fd.h>
#include <sys/wait.h>
#include "streamcache.h"
#include "fs.h"

extern int errno;

void mmount(int argc, char **argv, int type)
{
	char drive;
	int pid;
	int status;
	struct device dev;
	char name[EXPAND_BUF];
	int media;
	struct bootsector boot;
	Stream_t *Stream;
	
	if (argc<2 || !argv[1][0]) {
		fprintf(stderr,"Usage: %s -V drive:\n", argv[0]);
		cleanup_and_exit(1);
	}
	drive = argv[1][0];
	Stream = find_device(drive, O_RDONLY, &dev, &boot, name, &media);
	if(!Stream)
		cleanup_and_exit(1);
	FREE(&Stream);

	/* and finally mount it */
	switch((pid=fork())){
	case -1:
		fprintf(stderr,"fork failed\n");
		cleanup_and_exit(1);
	case 0:
		close(2);
		open("/dev/null", O_RDWR);
		argv[1] = "mount" ;
		if ( argc > 2 )
			execvp("mount", argv + 1 );
		else
			execlp("mount", "mount", name, 0);
		perror("exec mount");
		cleanup_and_exit(1);
	default:
		while ( wait(&status) != pid );
	}	
	if ( WEXITSTATUS(status) == 0 )
		cleanup_and_exit(0);
	argv[0] = "mount";
	argv[1] = "-r";
	if ( argc > 2 )
		execvp("mount", argv);
	else
		execlp("mount", "mount","-r", name, 0);
	cleanup_and_exit(1);
}

#else /* linux */

#include "msdos.h"

void mmount(int argc, char **argv, int type)
{
  fprintf(stderr,"This command is only available for LINUX \n");
  cleanup_and_exit(1);
}
#endif /* linux */


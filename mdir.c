/*
 * mdir.c:
 * Display an MSDOS directory
 */

#include "sysincludes.h"
#include "msdos.h"
#include "vfat.h"
#include "mtools.h"
#include "file.h"
#include "streamcache.h"
#include "fs.h"
#include "codepage.h"


/*
 * Print an MSDOS directory date stamp.
 */
static inline void print_date(struct directory *dir)
{
	printf("%02d-%02d-%02d", DOS_MONTH(dir), DOS_DAY(dir), DOS_YEAR(dir));
}

/*
 * Print an MSDOS directory time stamp.
 */
static inline void print_time(struct directory *dir)
{
	char am_pm;
	int hour = DOS_HOUR(dir);
       
	am_pm = (hour >= 12) ? 'p' : 'a';
	if (hour > 12)
		hour = hour - 12;
	if (hour == 0)
		hour = 12;

	printf("%2d:%02d%c", hour, DOS_MINUTE(dir), am_pm);
}

/*
 * Return a number in dotted notation
 */
static char *dotted_num(unsigned long num, int width, char **buf)
{
	int      len;
	register char *srcp, *dstp;
	int size;

	size = width + width;
	*buf = malloc(size+1);

	if (*buf == NULL)
		return "";
	
	/* Create the number in maximum width; make sure that the string
	 * length is not exceeded (in %6ld, the result can be longer than 6!)
	 */
	sprintf(*buf, "%.*ld", size, num);

	for (srcp=*buf; srcp[1] != '\0'; ++srcp)
		if (srcp[0] == '0')
			srcp[0] = ' ';
		else
			break;
	
	len = strlen(*buf);
	srcp = (*buf)+len;
	dstp = (*buf)+len+1;

	for ( ; dstp >= (*buf)+4 && isdigit (srcp[-1]); ) {
		srcp -= 3;  /* from here we copy three digits */
		dstp -= 4;  /* that's where we put these 3 digits */
	}

	/* now finally copy the 3-byte blocks to their new place */
	while (dstp < (*buf) + len) {
		dstp[0] = srcp[0];
		dstp[1] = srcp[1];
		dstp[2] = srcp[2];
		if (dstp + 3 < (*buf) + len)
			/* use spaces instead of dots: they place both
			 * Americans and Europeans */
			dstp[3] = ' ';		
		srcp += 3;
		dstp += 4;
	}

	return (*buf) + len-width;
}

static inline void print_volume_label(Stream_t *Stream, char drive)
{
	DeclareThis(FsPublic_t);
	Stream_t *RootDir;
	int entry;
	struct directory dir;
	char shortname[13];
	char longname[VBUFSIZE];
	
	/* find the volume label */
	RootDir = open_root(COPY(Stream));
	entry = 0;
	if(vfat_lookup(RootDir, Stream, &dir, &entry, 0, 0,
		       ACCEPT_LABEL | MATCH_ANY,
		       NULL, shortname, longname, NULL) )
		printf(" Volume in drive %c has no label\n", drive);
	else if (*longname)
		printf(" Volume in drive %c is %s (abbr=%s)\n",
		       drive, longname, shortname);
	else
		printf(" Volume in drive %c is %s\n",
		       drive, shortname);
	if(This->serialized)
		printf(" Volume Serial Number is %04lX-%04lX\n",
		       (This->serial_number >> 16) & 0xffff, 
		       This->serial_number & 0xffff);
	FREE(&RootDir);
}


static inline int list_directory(Stream_t *Dir, Stream_t *Fs, char *newname, 
			  int wide, int all,
			  long *tot_size)
{
	int entry;
	struct directory dir;
	int files;
	long size;
	char shortname[13];
	char longname[VBUFSIZE];
	int i;
	int Case;

	entry = 0;
	files = 0;

	if(!wide)
		printf("\n");

	while(vfat_lookup(Dir, Fs, &dir, &entry, 0, newname,
			  ACCEPT_DIR | ACCEPT_PLAIN,
			  NULL, shortname, longname, NULL) == 0){
		if(!all && (dir.attr & 0x6))
			continue;
		if (wide && ! (files % 5))
			putchar('\n');
		files++;

		if(dir.attr & 0x10){
			size = 0;
		} else
			size = FILE_SIZE(&dir);

		Case = dir.Case;
		if(!(Case & (BASECASE | EXTCASE)) && mtools_ignore_short_case)
			Case |= BASECASE | EXTCASE;

		if(Case & EXTCASE){
			for(i=0; i<3;i++)
				dir.ext[i] = tolower(dir.ext[i]);
		}
		to_unix(dir.ext,3);
		if(Case & BASECASE){
			for(i=0; i<8;i++)
				dir.name[i] = tolower(dir.name[i]);
		}
		to_unix(dir.name,8);
		if(wide){
			if(dir.attr & 0x10)
				printf("[%s]%*s", shortname,
					(int) (16 - 2 - strlen(shortname)), "");
			else
				printf("%-16s", shortname);
		} else {				
			/* is a subdirectory */
			printf("%-8.8s %-3.3s ",dir.name, dir.ext);
			if(dir.attr & 0x10)
				printf("<DIR>    ");
			else
				printf(" %8ld", size);
			printf(" ");
			print_date(&dir);
			printf("  ");
			print_time(&dir);

			if(*longname)
				printf(" %s", longname);
			printf("\n");
		}
		*tot_size += size;
	}

	if(wide)
		putchar('\n');
	return files;
}

static inline void print_footer(char *drive, char *newname, int files,
			 long tot_size, long blocks)
{	
	char *s1,*s2;

	if (*drive != '\0') {
		if (!files)
			printf("File \"%s\" not found\n"
			       "                     %s bytes free\n\n",
			       newname, dotted_num(blocks,12, &s1));
		else {
			printf("      %3d file(s)     %s bytes\n"
			       "                      %s bytes free\n\n",
			       files, dotted_num(tot_size,12, &s1),
			       dotted_num(blocks,12, &s2));
			if(s2)
				free(s2);
		}
		if(s1)
			free(s1);
	}
	*drive = '\0'; /* ensure the footer is only printed once */
}

void mdir(int argc, char **argv, int type)
{
	char *arg;
	StreamCache_t sc;
	int i, files, fargn, wide, all, faked;
	long blocks, tot_size=0;
	char last_drive;
	char newpath[MAX_PATH];
	char drive;
	char newname[13];
	Stream_t *Fs;
	Stream_t *SubDir;
	Stream_t *Dir;

	fargn = 1;
	wide = all = files = blocks = 0;
					/* first argument */
	while (argc > fargn) {
		if (!strcmp(argv[fargn], "-w")) {
			wide = 1;
			fargn++;
			continue;
		}
		if (!strcmp(argv[fargn], "-a")) {
			all = 1;
			fargn++;
			continue;
		}
		if (argv[fargn][0] == '-') {
			fprintf(stderr, "%s: illegal option -- %c\n",
				argv[0], argv[1][1]);
			fprintf(stderr, "Mtools version %s, dated %s\n",
				mversion, mdate);
			fprintf(stderr, "Usage: %s: [-V] [-w] [-a] msdosdirectory\n",
				argv[0]);
			fprintf(stderr,
			"       %s: [-V] [-w] [-a] msdosfile [msdosfiles...]\n",
				argv[0]);
			cleanup_and_exit(1);
		}
		break;
	}

	/* fake an argument */
	faked = 0;
	if (argc == fargn) {
		faked++;
		argc++;
	}

	init_sc(&sc);
	last_drive = '\0';
	for (i = fargn; i < argc; i++) {
		if (faked)
			arg="";
		else 
			arg = argv[i];
		Dir = open_subdir(&sc, arg, O_RDONLY, &Fs);
		if(!Dir)
			continue;
		drive = sc.last_drive;

		/* is this a new device? */
		if (drive != last_drive) {
			print_footer(&last_drive, newname, files, tot_size,
				     blocks);
			blocks = getfree(Fs);
			files = 0;
			tot_size = 0;
			print_volume_label(Fs, drive);
		}
		last_drive = drive;

		/*
		 * Under MSDOS, wildcards that match directories don't
		 * display the contents of that directory.  So I guess I'll
		 * do that too.
		 */
		get_path(arg,newpath,sc.mcwd);
		if (strpbrk(sc.filename, "*[?") == NULL &&
		    ((SubDir = descend(Dir, Fs, sc.filename, 0, 0) )) ) {
			/* Subdirectory */			
			FREE(&Dir);
			Dir = SubDir;
			if(*sc.filename){
				if (newpath[strlen(newpath) -1 ] != '/')
					strcat(newpath, "/");
				strcat(newpath, sc.filename);
			}
			*sc.filename='\0';
		}

		if(sc.filename[0]=='\0')
			strcpy(newname, "*");
		else
			strcpy(newname, sc.filename);

		printf(" Directory for %c:%s\n", drive, newpath);
		files += list_directory(Dir, Fs, newname, wide, all, &tot_size);
		FREE(&Dir);
	}
	print_footer(&last_drive, newname, files, tot_size, blocks);
	finish_sc(&sc);
	cleanup_and_exit(0);
}

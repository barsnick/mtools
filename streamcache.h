#ifndef MTOOLS_STREAMCACHE_H
#define MTOOLS_STREAMCACHE_H

#include <sys/param.h>
#include "vfat.h"

typedef struct StreamCache_t {
	/* stuff needing to be initialised by the caller */
	int (*callback)(Stream_t *Dir, struct StreamCache_t *sc, int entry);
	int (*unixcallback)(char *name, struct StreamCache_t *sc);
	int (*newdoscallback)(char *name, struct StreamCache_t *sc);
	int (*newdrive_cb)(Stream_t *Dir, struct StreamCache_t *sc);
	int (*olddrive_cb)(Stream_t *Dir, struct StreamCache_t *sc);
	void *arg; /* to be passed to callback */

       	int openflags; /* flags used to open disk */
	int lookupflags; /* flags used to lookup up using vfat_lookup */

	char *outname; /* where to put the matched file name */
	char *shortname; /* where to put the short name of the matched file */
	char *longname; /* where to put the long name of the matched file */
	char *pathname; /* path name of file */
	char filename[VBUFSIZE];
	char drivename;

	/* out parameter */
	Stream_t *File;
	Stream_t *Fs; /* open drive corresponding to File, for callback */
	struct directory dir;

	/* internal data */
	char last_drive; /* last opened drive */	
	char mcwd[MAXPATHLEN];	
	Stream_t *fss[256]; /* open drives */
	char subdir_name[VBUFSIZE];
	Stream_t *subdir;	
} StreamCache_t;

Stream_t *open_subdir(StreamCache_t *StreamCache, char *arg, 
		      int flags, Stream_t **Fs);
void init_sc(StreamCache_t *StreamCache);
void finish_sc(StreamCache_t *StreamCache);
int main_loop(StreamCache_t *StreamCache, char *progname, 
	      char **argv, int argc);

#define NEXT_DISK 1
#define MISSED_ONE 2
#define GOT_ONE 4
#define IS_MATCH 8

#endif

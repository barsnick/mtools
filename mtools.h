#ifndef MTOOLS_MTOOLS_H
#define MTOOLS_MTOOLS_H

#include <sys/types.h>
#include <sys/stat.h>
#include "msdos.h"

typedef struct device {
	char *name;	       	/* full path to device */

	char drive;	       	/* the drive letter */
	int fat_bits;		/* FAT encoding scheme */

	int mode;	       	/* any special open() flags */
	unsigned int tracks;	/* tracks */
	unsigned int heads;	/* heads */
	unsigned int sectors;	/* sectors */

	long offset;	       	/* skip this many bytes */
	int use_xdf;

	/* internal variables */
	int file_nr;		/* used during parsing */
	unsigned int ssize;
	unsigned int use_2m;

	long partition;
} device_t;

#include "stream.h"


extern char *short_illegals, *long_illegals;

int maximize(int *target, int max);
int minimize(int *target, int max);

int init_geom(int fd, struct device *dev, struct device *orig_dev,
	      struct stat *stat);

int readwrite_sectors(int fd, /* file descriptor */
		      int *drive,
		      int rate,
		      int seektrack,
		      int track, int head, int sector, int size, /* address */
		      char *data, 
		      int bytes,
		      int direction,
		      int retries);

int lock_dev(int fd);

Stream_t *subdir(Stream_t *, char *pathname);

char *unix_normalize (char *ans, char *name, char *ext);
void dir_write(Stream_t *Dir, int num, struct directory *dir);
char *dos_name(char *filename, int verbose, int *mangled, char *buffer);
struct directory *mk_entry(char *filename, char attr,
			   unsigned int fat, long size, long date,
			   struct directory *ndir);
int copyfile(Stream_t *Source, Stream_t *Target);
long getfree(Stream_t *Stream);

FILE *opentty(int mode);

int is_dir(Stream_t *Dir, char *path);
Stream_t *descend(Stream_t *Dir, Stream_t *Fs, char *path, int barf,
		  char *outname);

int dir_grow(Stream_t *Dir, Stream_t *Fs, int size);
int match(__const char *, __const char *, char *, int);

Stream_t *new_file(char *filename, char attr,
		   unsigned int fat, long size, long date,
		   struct directory *ndir);
char *unix_name(char *name, char *ext, char Case, char *answer);
void *safe_malloc(size_t size);
Stream_t *open_filter(Stream_t *Next);

extern int got_signal;
void setup_signal(void);

UNUSED(static inline void set_ulong(unsigned long *address, int value));
static inline void set_ulong(unsigned long *address, int value)
{
	if(value)
		*address = value;
}

UNUSED(static inline void set_uint(unsigned int *address, int value));
static inline void set_uint(unsigned int *address, int value)
{
	if(value)
		*address = value;
}

UNUSED(static inline int compare (int ref, int testee));
static inline int compare (int ref, int testee)
{
	return (ref && ref != testee);
}


Stream_t *find_device(char drive, int mode, struct device *out_dev,
		      struct bootsector *boot,
		      char *name, int *media);

struct directory *labelit(char *dosname,
			  char *longname,
			  void *arg0,
			  struct directory *dir);

char *label_name(char *filename, int verbose, 
		 int *mangled, char *ans);

void cleanup_and_exit(int code);
/* environmental variables */
extern int mtools_skip_check;
extern int mtools_fat_compatibility;
extern int mtools_ignore_short_case;
extern int mtools_rate_0, mtools_rate_any, mtools_raw_tty;

void read_config(void);
extern struct device *devices;
extern struct device const_devices[];
extern const nr_const_devices;

#define New(type) ((type*)(malloc(sizeof(type))))
#define Grow(adr,n,type) ((type*)(realloc((char *)adr,n*sizeof(type))))
#define Free(adr) (free((char *)adr));
#define NewArray(size,type) ((type*)(calloc((size),sizeof(type))))

void mattrib(int argc, char **argv, int type);
void mbadblocks(int argc, char **argv, int type);
void mcd(int argc, char **argv, int type);
void mcopy(int argc, char **argv, int type);
void mdel(int argc, char **argv, int type);
void mdir(int argc, char **argv, int type);
void mformat(int argc, char **argv, int type);
void mlabel(int argc, char **argv, int type);
void mmd(int argc, char **argv, int type);
void mmount(int argc, char **argv, int type);
void mmove(int argc, char **argv, int type);
void mtest(int argc, char **argv, int type);

#endif

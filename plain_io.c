/*
 * Io to a plain file or device
 *
 * written by:
 *
 * Alain L. Knaff			
 * Alain.Knaff@inrialpes.fr
 *
 */

#include "sysincludes.h"
#include "stream.h"
#include "mtools.h"
#include "msdos.h"
#include "plain_io.h"
#include "patchlevel.h"

typedef struct SimpleFloppy_t {
	Class_t *Class;
	int refs;
	Stream_t *Next;
	Stream_t *Buffer;
	struct stat stat;
	int fd;
	int offset;
	int lastwhere;
	int seekable;
} SimpleFloppy_t;


/*
 * Create an advisory lock on the device to prevent concurrent writes.
 * Uses either lockf, flock, or fcntl locking methods.  See the Makefile
 * and the Configure files for how to specify the proper method.
 */

int lock_dev(int fd)
{
#if (defined(HAVE_LOCKF) && defined(F_TLOCK))
	if (lockf(fd, F_TLOCK, 0) < 0)
#else /* LOCKF */

#if (defined(HAVE_FLOCK) && defined (LOCK_EX) && defined(LOCK_NB))
	if (flock(fd, LOCK_EX|LOCK_NB) < 0)
#else /* FLOCK */

#if (defined(F_SETLK) && defined(F_WRLCK))
	struct flock flk;

	flk.l_type = F_WRLCK;
	flk.l_whence = 0;
	flk.l_start = 0L;
	flk.l_len = 0L;

	if (fcntl(fd, F_SETLK, &flk) < 0)
#endif /* FCNTL */
#endif /* FLOCK */
#endif /* LOCKF */
	{
		if(errno == EINVAL)
			return 0;
		else
			return 1;
	}
	return 0;
}

typedef int (*iofn) (int, char *, int);

static int f_io(Stream_t *Stream, char *buf, int where, int len,
		iofn io)
{
	DeclareThis(SimpleFloppy_t);
	int ret;

	where += This->offset;
	if (This->seekable && where != This->lastwhere ){
		if(lseek( This->fd, where, SEEK_SET) < 0 ){
			perror("seek");
			This->lastwhere = -1;
			return -1;
		}
	}
	ret = io(This->fd, buf, len);
	if ( ret == -1 ){
		perror("read");
		This->lastwhere = -1;
		return -1;
	}
	This->lastwhere = where + ret;
	return ret;
}
	


static int f_read(Stream_t *Stream, char *buf, int where, int len)
{	
	return f_io(Stream, buf, where, len, (iofn) read);
}

static int f_write(Stream_t *Stream, char *buf, int where, int len)
{
	return f_io(Stream, buf, where, len, (iofn) write);
}

static int f_flush(Stream_t *Stream)
{
#if 0
	DeclareThis(SimpleFloppy_t);

	return fsync(This->fd);
#endif
	return 0;
}

static int f_free(Stream_t *Stream)
{
	DeclareThis(SimpleFloppy_t);

	if (This->fd > 2)
		return close(This->fd);
	else
		return 0;
}

#if 0
int check_parameters(struct device *ref, struct device *testee)
{
	return 0;
}
#endif

static int f_geom(Stream_t *Stream, struct device *dev, 
		  struct device *orig_dev,
		  int media, struct bootsector *boot)
{
	int ret;
	DeclareThis(SimpleFloppy_t);
	unsigned long tot_sectors;
	int BootP, Infp0, InfpX, InfTm;
	int sectors, j;
	unsigned char sum;
	int sect_per_track;

	dev->ssize = 2; /* allow for init_geom to change it */
	dev->use_2m = 0x80; /* disable 2m mode to begin */

	if(media == 0xf0 || media >= 0x100){		
		dev->heads = WORD(nheads);
		dev->sectors = WORD(nsect);
		tot_sectors = DWORD(bigsect);
		set_ulong(&tot_sectors, WORD(psect));
		sect_per_track = dev->heads * dev->sectors;
		tot_sectors += sect_per_track - 1; /* round size up */
		dev->tracks = tot_sectors / sect_per_track;

		BootP = WORD(BootP);
		Infp0 = WORD(Infp0);
		InfpX = WORD(InfpX);
		InfTm = WORD(InfTm);
		
		if (boot->descr >= 0xf0 &&
		    boot->dos4 == 0x29 &&
		    strncmp( boot->banner,"2M", 2 ) == 0 &&
		    BootP < 512 && Infp0 < 512 && InfpX < 512 && InfTm < 512 &&
		    BootP >= InfTm + 2 && InfTm >= InfpX && InfpX >= Infp0 && 
		    Infp0 >= 76 ){
			for (sum=0, j=63; j < BootP; j++) 
				sum += boot->jump[j];/* checksum */
			dev->ssize = boot->jump[InfTm];
			if (!sum && 
			    dev->ssize >= 0 && dev->ssize <= 7 ){
				dev->use_2m = 0xff;
				dev->ssize |= 0x80; /* is set */
			}
		}
	} else if (media >= 0xf8){
		media &= 3;
		dev->heads = old_dos[media].heads;
		dev->tracks = old_dos[media].tracks;
		dev->sectors = old_dos[media].sectors;
		dev->ssize = 0x80;
		dev->use_2m = ~1;
	} else {
		fprintf(stderr,"Unknown media type\n");
		cleanup_and_exit(1);
	}

	sectors = dev->sectors;
	dev->sectors = dev->sectors * WORD(secsiz) / 512;

	ret = init_geom(This->fd,dev, orig_dev, &This->stat);
	dev->sectors = sectors;
	return ret;
}


static int f_data(Stream_t *Stream, long *date, unsigned long *size,
		  int *type, int *address)
{
	DeclareThis(SimpleFloppy_t);

	if(date)
		*date = This->stat.st_mtime;
	if(size)
		*size = This->stat.st_size;
	if(type)
		*type = S_ISDIR(This->stat.st_mode);
	if(address)
		*address = 0;
	return 0;
}

static Class_t SimpleFloppyClass = {
	f_read, 
	f_write,
	f_flush,
	f_free,
	f_geom,
	f_data
};

Stream_t *SimpleFloppyOpen(struct device *dev, struct device *orig_dev,
			   char *name, int mode, char *errmsg)
{
	SimpleFloppy_t *This;

	This = New(SimpleFloppy_t);
	if (!This){
		fprintf(stderr,"Out of memory error\n");
		cleanup_and_exit(1);
	}
	This->seekable = 1;

	This->Class = &SimpleFloppyClass;
	if (strcmp(name,"-") == 0 ){
		if (mode == O_RDONLY)
			This->fd = 0;
		else
			This->fd = 1;
		This->seekable = 0;
		This->refs = 1;
		This->Next = 0;
		This->Buffer = 0;
		return (Stream_t *) This;
	}
	
	if(dev)
		mode |= dev->mode;

	This->fd = open(name, mode, 0666);

	if (This->fd < 0) {		
		Free(This);
		if(errmsg)
			sprintf(errmsg, "Can't open %s: %s",
				name, strerror(errno));
		return NULL;
	}

	if (fstat(This->fd, &This->stat) < 0){
		Free(This);
		if(errmsg)
			sprintf(errmsg,"Can't stat %s: %s", 
				name, strerror(errno));

		return NULL;
	}

	/* lock the device on writes */
	if (mode == O_RDWR && lock_dev(This->fd)) {
		close(This->fd);
		Free(This);
		if(errmsg)
			sprintf(errmsg,
				"plain floppy: device \"%s\"busy\n:", 
				dev->name);
		return NULL;
	}
		
	/* set default parameters, if needed */
	if (dev){
		if (init_geom(This->fd, dev, orig_dev, &This->stat)){
			close(This->fd);
			Free(This);
			if(errmsg)
				sprintf(errmsg,"init: set default params");
			return NULL;
		}
		This->offset = dev->offset;
	} else
		This->offset = 0;

	This->refs = 1;
	This->Next = 0;
	This->Buffer = 0;

	/* partitioned drive */
	while(dev && dev->partition && dev->partition <= 4) {
		unsigned char ptable[512];
		/* read the first sector, or part of it */
		if (force_read((Stream_t *)This, (char*) ptable, 0, 512) != 512)
			break;
		if( _WORD(ptable+510) != 0xaa55)
			break;
		This->offset += (_DWORD(ptable+0x1a6+(dev->partition<<5))) << 9;
		break;
	}
	return (Stream_t *) This;
}

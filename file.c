#include "sysincludes.h"
#include "msdos.h"
#include "stream.h"
#include "mtools.h"
#include "fsP.h"
#include "file.h"
#include "htable.h"

typedef struct File_t {
	Class_t *Class;
	int refs;
	struct Fs_t *Fs;	/* Filesystem that this fat file belongs to */
	Stream_t *Buffer;

	int (*map)(struct File_t *this, int where, int *len, int mode);
	unsigned long FileSize;

	/* Absolute position of first cluster of file */
	unsigned short FirstAbsCluNr;

	/* Absolute position of previous cluster */
	unsigned short PreviousAbsCluNr;

	/* Relative position of previous cluster */
	unsigned short PreviousRelCluNr;
	struct directory dir;
} File_t;

static int normal_map(File_t *This, int where, int *len, int mode)
{
	int offset;
	int NrClu; /* number of clusters to read */
	unsigned short RelCluNr;
	unsigned short CurCluNr;
	unsigned short NewCluNr;
	unsigned short AbsCluNr;
	int clus_size;
	Fs_t *Fs = This->Fs;

	clus_size = Fs->cluster_size * Fs->sector_size;
	offset = where % clus_size;

	if (mode == MT_READ)
		maximize(len, This->FileSize - where);
	if (*len <= 0 ){
		*len = 0;
		return 0;
	}

	if (This->FirstAbsCluNr < 2){
		if( mode == MT_READ || *len == 0){
			*len = 0;
			return 0;
		}
		NewCluNr = get_next_free_cluster(This->Fs, 1);
		if (NewCluNr == 1 ){
			errno = ENOSPC;
			return -2;
		}
		This->FirstAbsCluNr = NewCluNr;
		Fs->fat_encode(This->Fs, NewCluNr, Fs->end_fat);
	}

	RelCluNr = where / clus_size;
	
	if (RelCluNr >= This->PreviousRelCluNr){
		CurCluNr = This->PreviousRelCluNr;
		AbsCluNr = This->PreviousAbsCluNr;
	} else {
		CurCluNr = 0;
		AbsCluNr = This->FirstAbsCluNr;
	}


	NrClu = (offset + *len - 1) / clus_size;
	while (CurCluNr <= RelCluNr + NrClu){
		if (CurCluNr == RelCluNr){
			/* we have reached the beginning of our zone. Save
			 * coordinates */
			This->PreviousRelCluNr = RelCluNr;
			This->PreviousAbsCluNr = AbsCluNr;
		}
		NewCluNr = Fs->fat_decode(This->Fs, AbsCluNr);
		if (NewCluNr == 1 ){
			fprintf(stderr,"Fat problem while decoding %d\n", 
				AbsCluNr);
			cleanup_and_exit(1);
		}
		if(CurCluNr == RelCluNr + NrClu)			
			break;
		if (NewCluNr == Fs->end_fat && mode == MT_WRITE){
			/* if at end, and writing, extend it */
			NewCluNr = get_next_free_cluster(This->Fs, AbsCluNr);
			if (NewCluNr == 1 ){ /* no more space */
				errno = ENOSPC;
				return -2;
			}
			Fs->fat_encode(This->Fs, AbsCluNr, NewCluNr);
			Fs->fat_encode(This->Fs, NewCluNr, Fs->end_fat);
		}

		if (CurCluNr < RelCluNr && NewCluNr == Fs->end_fat){
			*len = 0;
			return 0;
		}

		if (CurCluNr >= RelCluNr && NewCluNr != AbsCluNr + 1)
			break;
		CurCluNr++;
		AbsCluNr = NewCluNr;
	}

	maximize(len, (1 + CurCluNr - RelCluNr) * clus_size - offset);

	return ((This->PreviousAbsCluNr - 2) * Fs->cluster_size +
		Fs->dir_start + Fs->dir_len) *
			Fs->sector_size + offset;
}


static int root_map(File_t *This, int where, int *len, int mode)
{
	Fs_t *Fs = This->Fs;

	maximize(len, Fs->dir_len * Fs->sector_size - where);
	if(*len < 0 ){
		*len = 0;
		errno = ENOSPC;
		return -2;
	}
	return Fs->dir_start * Fs->sector_size + where;
}
	

static int read_file(Stream_t *Stream, char *buf, int where, int len)
{
	DeclareThis(File_t);
	int pos;

	Stream_t *Disk = This->Fs->Next;
	
	pos = This->map(This, where, &len, MT_READ);
	if(pos < 0)
		return pos;
	return READS(Disk, buf, pos, len);
}

static int write_file(Stream_t *Stream, char *buf, int where, int len)
{
	DeclareThis(File_t);
	int pos;
	int ret;
	Stream_t *Disk = This->Fs->Next;

	pos = This->map(This, where, &len, MT_WRITE);
	if( pos < 0)
		return pos;
	ret = WRITES(Disk, buf, pos, len);
	if ( ret > 0 && where + ret > This->FileSize )
		This->FileSize = where + ret;
	return ret;
}


/*
 * Convert an MSDOS time & date stamp to the Unix time() format
 */

static int month[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
static inline long conv_stamp(struct directory *dir)
{
	struct tm *tmbuf;
	long tzone, dst;
	long accum;

	accum = DOS_YEAR(dir) - 1970; /* years past */

	/* days passed */
	accum = accum * 365L + month[DOS_MONTH(dir)-1] + DOS_DAY(dir);

	/* leap years */
	accum += (DOS_YEAR(dir) - 1972) / 4L;

	/* back off 1 day if before 29 Feb */
	if (!(DOS_YEAR(dir) % 4) && DOS_MONTH(dir) < 3)
	        accum--;
	accum = accum * 24L + DOS_HOUR(dir); /* hours passed */
	accum = accum * 60L + DOS_MINUTE(dir); /* minutes passed */
	accum = accum * 60L + DOS_SEC(dir); /* seconds passed */

	/* correct for Time Zone */
#ifdef HAVE_GETTIMEOFDAY
	{
		struct timeval tv;
		struct timezone tz;
		
		gettimeofday(&tv, &tz);
		tzone = tz.tz_minuteswest * 60L;
	}
#else
#ifdef HAVE_TZSET
	{
		extern long timezone;
		tzset();
		tzone = timezone;
	}
#else
	tzone = 0;
#endif /* HAVE_TZSET */
#endif /* HAVE_GETTIMEOFDAY */
	
	accum += tzone;

	/* correct for Daylight Saving Time */
	tmbuf = localtime(&accum);
	dst = (tmbuf->tm_isdst) ? (-60L * -60L) : 0L;
	accum += dst;
	
	return accum;
}


static int get_file_data(Stream_t *Stream, long *date, unsigned long *size,
			 int *type, int *address)
{
	DeclareThis(File_t);

	if(date)
		*date = conv_stamp(& This->dir);
	if(size)
		*size = This->FileSize;
	if(type)
		*type = This->dir.attr & 0x10;
	if(address)
		*address = This->FirstAbsCluNr;
	return 0;
}

T_HashTable *filehash;

static int free_file(Stream_t *Stream)
{
	return hash_remove(filehash, (void *) Stream, 0);
}

static Class_t FileClass = { 
	read_file, 
	write_file, 
	0, /* flush */
	free_file, /* free */
	0, /* get_geom */
	get_file_data 
};


static unsigned int func1(void *Stream)
{
	DeclareThis(File_t);

	return This->FirstAbsCluNr ^ (int) This->Fs;
}

static unsigned int func2(void *Stream)
{
	DeclareThis(File_t);

	return This->FirstAbsCluNr;
}

static int comp(void *Stream, void *Stream2)
{
	DeclareThis(File_t);
	File_t *This2 = (File_t *) Stream2;

	return This->Fs != This2->Fs ||
		This->FirstAbsCluNr != This2->FirstAbsCluNr;
}

static void init_hash(void)
{
	static int is_initialised=0;
	
	if(!is_initialised){
		make_ht(func1, func2, comp, 20, &filehash);
		is_initialised = 1;
	}
}


Stream_t *open_root(Stream_t *Fs)
{
	File_t *File;
	File_t Pattern;

	init_hash();
	Pattern.Fs = (Fs_t *) Fs;
	Pattern.FirstAbsCluNr = 0;
	if(!hash_lookup(filehash, (T_HashTableEl) &Pattern, 
			(T_HashTableEl **)&File, 0)){
		File->refs++;
		Fs->refs--;
		return (Stream_t *) File;
	}

	File = New(File_t);
	File->Fs = (Fs_t *) Fs;
	if (!File)
		return NULL;
	
	File->FileSize = -1;
	File->Class = &FileClass;
	File->map = root_map;
	File->refs = 1;
	File->Buffer = 0;
	hash_add(filehash, (void *) File);
	return (Stream_t *) File;
}

Stream_t *open_file(Stream_t *Fs, struct directory *dir)
{
	File_t *File;
	File_t Pattern;
	int first;
	unsigned long size;

	first = START(dir);

	if(!first && (dir->attr & 0x10))
		return open_root(Fs);
	
	if (dir->attr & 0x10 )
		size = (1UL << 31) - 1;
	else 
		size = FILE_SIZE(dir);


	init_hash();
	if(first >= 2){
		/* if we have a zero-addressed file here, it is certainly
		 * _not_ the root directory, but rather a newly created
		 * file */
		Pattern.Fs = (Fs_t *) Fs;
		Pattern.FirstAbsCluNr = first;
		if(!hash_lookup(filehash, (T_HashTableEl) &Pattern, 
				(T_HashTableEl **)&File, 0)){
			File->refs++;
			Fs->refs--;
			return (Stream_t *) File;
		}
	}

	File = New(File_t);
	if (!File)
		return NULL;
	
	/* memorize dir for date and attrib */
	File->dir = *dir;
	File->Class = &FileClass;
	File->Fs = (Fs_t *) Fs;
	File->map = normal_map;
	File->FirstAbsCluNr = first;
	File->PreviousRelCluNr = 0xffff;
	File->FileSize = size;
	File->refs = 1;
	File->Buffer = 0;
	hash_add(filehash, (void *) File);
	return (Stream_t *) File;
}

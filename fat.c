#include "sysincludes.h"
#include "msdos.h"
#include "stream.h"
#include "mtools.h"
#include "fsP.h"

extern Stream_t *default_drive;

/*
 * Fat 12 encoding:
 *	|    byte n     |   byte n+1    |   byte n+2    |
 *	|7|6|5|4|3|2|1|0|7|6|5|4|3|2|1|0|7|6|5|4|3|2|1|0|
 *	| | | | | | | | | | | | | | | | | | | | | | | | |
 *	| n+0.0 | n+0.5 | n+1.0 | n+1.5 | n+2.0 | n+2.5 |
 *	    \_____  \____   \______/________/_____   /
 *	      ____\______\________/   _____/  ____\_/
 *	     /     \      \          /       /     \
 *	| n+1.5 | n+0.0 | n+0.5 | n+2.0 | n+2.5 | n+1.0 |
 *	|      FAT entry k      |    FAT entry k+1      |
 */
 
 /*
 * Get and decode a FAT (file allocation table) entry.  Returns the cluster
 * number on success or 1 on failure.
 */

static unsigned int fat12_decode(Fs_t *Stream, unsigned int num)
{
	DeclareThis(Fs_t);
	unsigned int start = num * 3 / 2;
	unsigned char *address = This->fat_buf + start;

	if (num < 2 || start + 1 > (This->fat_len * This->sector_size))
		return 1;
	
	if (num & 1)
		return ((address[1] & 0xff) << 4) | ((address[0] & 0xf0 ) >> 4);
	else
		return ((address[1] & 0xf) << 8) | (address[0] & 0xff );
}




/*
 * Puts a code into the FAT table.  Is the opposite of fat_decode().  No
 * sanity checking is done on the code.  Returns a 1 on error.
 */
static int fat12_encode(Fs_t *Stream, unsigned int num, unsigned int code)
{
	DeclareThis(Fs_t);

	int start = num * 3 / 2;
	unsigned char *address=This->fat_buf + start;

	if (num < 2 || start + 1 > (This->fat_len * This->sector_size))
		return(1);

	This->fat_dirty = 1;
	if (num & 1) {
		/* (odd) not on byte boundary */
		address[0] = (address[0] & 0x0f) | ((code << 4) & 0xf0);
		address[1] = (code >> 4) & 0xff;
	} else {
		/* (even) on byte boundary */
		address[0] = code & 0xff;
		address[1] = (address[1] & 0xf0) | ((code >> 8) & 0x0f);
	}
	return 0;
}


/*
 * Fat 16 encoding:
 *	|    byte n     |   byte n+1    |
 *	|7|6|5|4|3|2|1|0|7|6|5|4|3|2|1|0|
 *	| | | | | | | | | | | | | | | | |
 *	|         FAT entry k           |
 */

static unsigned int fat16_decode(Fs_t *Stream, unsigned int num)
{
	DeclareThis(Fs_t);

	unsigned int start = num * 2;
	unsigned char *address = This->fat_buf + start;

	if (num < 2 || start + 1 > (This->fat_len * This->sector_size))
		return(1);
	return address[0] + (address[1] << 8 );
}

static int fat16_encode(Fs_t *Stream, unsigned int num, unsigned int code)
{       
	DeclareThis(Fs_t);

	int start = num * 2;
	unsigned char *address = This->fat_buf + start;

	if (num < 2 || start + 1 > (This->fat_len * This->sector_size))
		return(1);
	This->fat_dirty = 1;
	address[0] =  code & 0xff;
	address[1] =  code >> 8;
	return(0);
}

/*
 * Write the FAT table to the disk.  Up to now the FAT manipulation has
 * been done in memory.  All errors are fatal.  (Might not be too smart
 * to wait till the end of the program to write the table.  Oh well...)
 */

void fat_write(Fs_t *This)
{
	int i, dups, ret;

	if (!This->fat_dirty)
		return;

	dups = This->num_fat;
	if (This->fat_error)
		dups = 1;
	
	for(i=0; i<dups; i++){
		ret = force_write(This->Next,
				  (char *) This->fat_buf,
				  (This->fat_start + i*This->fat_len) * 
				  This->sector_size,
				  This->fat_len * This->sector_size);
		if (ret < This->fat_len * This->sector_size){
			if (ret < 0 ){
				perror("read error in fat_write");
				cleanup_and_exit(1);
			} else {
				fprintf(stderr,"end of file in fat_write\n");
				cleanup_and_exit(1);
			}
		}	 
	}
	This->fat_dirty = 0;
}


static inline int try_fat12(Fs_t *This)
{
	if (This->num_clus >= FAT12)
		/* too many clusters */
		return -2;

	This->end_fat = 0xfff;
	This->last_fat = 0xff6;
	This->fat_decode = fat12_decode;
	This->fat_encode = fat12_encode;
	return 0;
}

static inline int try_fat16(Fs_t *This)
{
	if(This->fat_len < NEEDED_FAT_SIZE(This))
		return -2;
	if(This->fat_buf[3] != 0xff)
		return -1;

	This->end_fat = 0xffff;
	This->last_fat = 0xfff6;
	This->fat_decode = fat16_decode;
	This->fat_encode = fat16_encode;
	return 0;
}

static inline int check_fat(Fs_t *This, int verbose)
{
	int i, f;
	if(mtools_skip_check)
		return 0;
	
	for ( i= 3 ; i <This->num_clus; i++){
		f = This->fat_decode(This,i);
		if (f < This->last_fat && f > This->num_clus){
			if(verbose){
				fprintf(stderr,
					"Cluster # at %d too big(%#x)\n", i,f);
				fprintf(stderr,"Probably non MS-DOS disk\n");
			}
			return -1;
		}
	}
	return 0;
}

static inline int try_fat(Fs_t *This, int bits, int verbose)
{
	int ret;

	This->fat_bits = bits;
	if (bits == 12)
		ret = try_fat12(This);
	else
		ret = try_fat16(This);
	if(ret)
		return ret;
	else
		return check_fat(This, verbose);
}


/*
 * Read the entire FAT table into memory.  Crude error detection on wrong
 * FAT encoding scheme.
 */
static int _fat_read(Fs_t *This, struct bootsector *boot, 
		     int fat_bits, int nfat)
{
	int ret, ret2;

	/* read the FAT sectors */
	ret = force_read(This->Next,
			 (char *) This->fat_buf,
			 (This->fat_start + nfat * This->fat_len) *
			 This->sector_size,
			 This->fat_len * This->sector_size);
	if (ret < This->fat_len * This->sector_size){
		if (ret < 0 ){
			perror("read error in fat_read");
			return -1;
		} else {
			fprintf(stderr,"end of file in fat_read\n");
			return -1;
		}
	}

	if (!mtools_skip_check &&
	    (This->fat_buf[0] || This->fat_buf[1] || This->fat_buf[2])) {
		if((This->fat_buf[0] != boot->descr && boot->descr >= 0xf0 &&
		    (This->fat_buf[0] != 0xf9 || boot->descr != 0xf0)) ||
		   This->fat_buf[0] < 0xf0){
			fprintf(stderr,
				"Bad media type, probably non-MSDOS disk\n");
			return -1;
		}
		if(This->fat_buf[1] != 0xff || This->fat_buf[2] != 0xff){
			fprintf(stderr,"Initial byte of fat is not 0xff\n");
			return -1;
		}
	}

	switch(fat_bits){
	case 12:
	case 16:
		return try_fat(This, fat_bits, 1);
	case -12:
	case -16:
		return try_fat(This, -fat_bits, 1);
	case 0:
		/* no size given in the configuration file.
		 * Figure out a first guess */

		if (boot->descr < 0xf0 || boot->dos4 !=0x29)
			fat_bits = 12; /* old DOS */
		else if (!strncmp(boot->fat_type, "FAT12   ", 8))
			fat_bits = 12;
		else if (!strncmp (boot->fat_type, "FAT16   ", 8))
			fat_bits = 16;
		else
			fat_bits = 12;

		ret = try_fat(This, fat_bits, nfat);
		if(!ret)
			return 0; /* first try succeeded */
		fat_bits = 16 - fat_bits;
		ret2 = try_fat(This, fat_bits, 1);
		if(ret2 >= ret) /* second try didn't fail as badly as first */
			return ret2;

		/* revert to first try because that one failed less badly */
		fat_bits = 16 - fat_bits;
		return try_fat(This, fat_bits, 1);
	default:
		fprintf(stderr,"%d fat bits not supported\n", fat_bits);
		return -3;
	}
}


/*
 * Read the entire FAT table into memory.  Crude error detection on wrong
 * FAT encoding scheme.
 */
int fat_read(Fs_t *This, struct bootsector *boot, int fat_bits)
{
	int i;
	unsigned int buflen;
	int ret;

	/* only the first copy of the FAT */
	buflen = This->fat_len * This->sector_size;
	This->fat_buf = (unsigned char *) malloc(buflen);
	if (This->fat_buf == NULL) {
		perror("fat_read: malloc");
		return -1;
	}

	This->fat_error = 0;
	This->fat_dirty = 0;

	for (i=0; i< This->num_fat; i++){
		if(i)
			fprintf(stderr,
				"Bad FAT for drive %c, trying secondary copy\n",
				This->drive);
		ret =  _fat_read(This, boot, fat_bits,i);
		switch(ret){
		case 0:
			goto break_2;
		case -1:
			continue;
		case -2:
			/* the return value suggests a different fat,
			 * independently of the read data */
			if(fat_bits > 0 ){				
				fprintf(stderr,
					"%c: %d bit FAT. sure ? (Use -%d in the device configuration file to bypass.)\n",
					This->drive, fat_bits, fat_bits);
				return -2;
			} else if(fat_bits < 0)
				/* the user insisted. Let it pass */
				goto break_2;
			/* FALLTHROUGH */
		case -3:
			return -1;
		}
	}

 break_2:
	if(i == This->num_fat){
		fprintf(stderr, "Could not read FAT for %c\n", This->drive);
		return -1;
	}

	/*
	 * Let's see if the length of the FAT table is appropriate for
	 * the number of clusters and the encoding scheme.
	 * Older versions of mtools goofed this up. If the env var
	 * MTOOLS_FAT_COMPATIBILITY is defined, skip this check in order to read
	 * disks formatted by an old version.
	 */
	if(!mtools_fat_compatibility &&
	   This->fat_len > NEEDED_FAT_SIZE(This) + 1){
		fprintf(stderr,
			"fat_read: Wrong FAT encoding for drive %c."
			"(len=%d instead of %d?)\n",
			This->drive, This->fat_len, NEEDED_FAT_SIZE(This));
		fprintf(stderr,
			"Set MTOOLS_FAT_COMPATIBILITY to suppress"
			" this message\n");
		return -1;
	}
	return 0;
}


unsigned int get_next_free_cluster(Fs_t *This, unsigned int last)
{
	int i;

	if ( last < 2)
		last = 1;
	for (i=last+1; i< This->num_clus+2; i++){
		if (!This->fat_decode(This, i))
			return i;
	}
	for(i=2; i < last+1; i++){
		if (!This->fat_decode(This, i))
			return i;
	}
	return 1;
}

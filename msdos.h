#ifndef MTOOLS_MSDOS_H
#define MTOOLS_MSDOS_H

#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>

/*
 * msdos common header file
 */

#define MAX_SECTOR	8192   		/* largest sector size */
#define MDIR_SIZE	32		/* MSDOS directory entry size in bytes*/
#define MAX_CLUSTER	8192		/* largest cluster size */
#define MAX_PATH	128		/* largest MSDOS path length */
#define MAX_DIR_SECS	64		/* largest directory (in sectors) */
#define MSECTOR_SIZE    msector_size

#define NEW		1
#define OLD		0

#define _WORD(x) ((unsigned char)(x)[0] + (((unsigned char)(x)[1]) << 8))
#define _DWORD(x) (_WORD(x) + (_WORD((x)+2) << 16))

#define DELMARK ((char) 0xe5)

struct directory {
	char name[8];			/* file name */
	char ext[3];			/* file extension */
	unsigned char attr;		/* attribute byte */
	unsigned char Case;		/* case of short filename */
	unsigned char reserved[9];	/* ?? */
	unsigned char time[2];		/* time stamp */
	unsigned char date[2];		/* date stamp */
	unsigned char start[2];		/* starting cluster number */
	unsigned char size[4];		/* size of the file */
};

#define EXTCASE 0x10
#define BASECASE 0x8

#define FILE_SIZE(dir)  (_DWORD((dir)->size))
#define START(dir) (_WORD((dir)->start))

static inline void set_dword(unsigned char *data, unsigned long value)
{
	data[3] = (value >> 24) & 0xff;
	data[2] = (value >> 16) & 0xff;
	data[1] = (value >>  8) & 0xff;
	data[0] = (value >>  0) & 0xff;
}

static inline void set_word(unsigned char *data, unsigned short value)
{
	data[1] = (value >>  8) & 0xff;
	data[0] = (value >>  0) & 0xff;
}


/*
 *	    hi byte     |    low byte
 *	|7|6|5|4|3|2|1|0|7|6|5|4|3|2|1|0|
 *      | | | | | | | | | | | | | | | | |
 *      \   7 bits    /\4 bits/\ 5 bits /
 *         year +80      month     day
 */
#define	DOS_YEAR(dir) (((dir)->date[1] >> 1) + 1980)
#define	DOS_MONTH(dir) (((((dir)->date[1]&0x1) << 3) + ((dir)->date[0] >> 5)))
#define	DOS_DAY(dir) ((dir)->date[0] & 0x1f)

/*
 *	    hi byte     |    low byte
 *	|7|6|5|4|3|2|1|0|7|6|5|4|3|2|1|0|
 *      | | | | | | | | | | | | | | | | |
 *      \  5 bits /\  6 bits  /\ 5 bits /
 *         hour      minutes     sec*2
 */
#define	DOS_HOUR(dir) ((dir)->time[1] >> 3)
#define	DOS_MINUTE(dir) (((((dir)->time[1]&0x7) << 3) + ((dir)->time[0] >> 5)))
#define	DOS_SEC(dir) (((dir)->time[0] & 0x1f) * 2)

#ifndef linux
#define BOOTSIZE 512
#else
#define BOOTSIZE 256
#endif

struct bootsector {
	unsigned char jump[3];		/* 0  Jump to boot code */
	char banner[8];			/* 3  OEM name & version */
	unsigned char secsiz[2];	/* 11 Bytes per sector hopefully 512 */
	unsigned char clsiz;		/* 13 Cluster size in sectors */
	unsigned char nrsvsect[2];	/* 14 Number of reserved (boot) sectors */
	unsigned char nfat;		/* 16 Number of FAT tables hopefully 2 */
	unsigned char dirents[2];	/* 17 Number of directory slots */
	unsigned char psect[2];		/* 19 Total sectors on disk */
	unsigned char descr;		/* 21 Media descriptor=first byte of FAT */
	unsigned char fatlen[2];	/* 22 Sectors in FAT */
	unsigned char nsect[2];		/* 24 Sectors/track */
	unsigned char nheads[2];	/* 26 Heads */
	unsigned char nhs[4];		/* 28 number of hidden sectors */
	unsigned char bigsect[4];	/* 32 big total sectors */
	unsigned char physdrive;	/* 36 physical drive ? */
	unsigned char reserved;		/* 37 reserved */
	unsigned char dos4;		/* 38 dos > 4.0 diskette */
	unsigned char serial[4];       	/* 39 serial number */
	char label[11];			/* 43 disk label */
	char fat_type[8];		/* 54 FAT type */

	unsigned char res_2m;		/* 62 reserved by 2M */
	unsigned char CheckSum;		/* 63 2M checksum (not used) */
	unsigned char fmt_2mf;		/* 64 2MF format version */
	unsigned char wt;		/* 65 1 if write track after format */
	unsigned char rate_0;		/* 66 data transfer rate on track 0 */
	unsigned char rate_any;		/* 67 data transfer rate on track<>0 */
	unsigned char BootP[2];		/* 68 offset to boot program */
	unsigned char Infp0[2];		/* 70 T1: information for track 0 */
	unsigned char InfpX[2];		/* 72 T2: information for track<>0 */
	unsigned char InfTm[2];		/* 74 T3: track sectors size table */
	unsigned char DateF[2];		/* 76 Format date */
	unsigned char TimeF[2];		/* 78 Format time */
	unsigned char junk[BOOTSIZE - 80];	/* 80 remaining data */
};


#define WORD(x) (_WORD(boot->x))
#define DWORD(x) (_DWORD(boot->x))
#define OFFSET(x) (((char *) (boot->x)) - ((char *)(boot->jump)))


extern struct OldDos_t {
	int tracks;
	int sectors;
	int heads;
	
	int dir_len;
	int cluster_size;
	int fat_len;

	int media;
} old_dos[];

#define FAT12 4086 /* max. number of clusters described by a 12 bit FAT */

extern const char *mversion;
extern const char *mdate;

/*
 * Function Prototypes
 */

int ask_confirmation(char *, char *, char *);
char *get_homedir(void);
#define EXPAND_BUF 2048
char *expand(char *, char *);
char *fix_mcwd(char *);
FILE *open_mcwd(char *mode);

char *get_name(char *, char *, char *mcwd);
char *get_path(char *, char *, char *mcwd);
char get_drive(char *, char);

int init(char drive, int mode);



#define MT_READ 1
#define MT_WRITE 2

#endif


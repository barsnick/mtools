/*
 * mformat.c
 */

#include "sysincludes.h"
#include "msdos.h"
#include "mtools.h"
#include "streamcache.h"
#include "fsP.h"
#include "file.h"
#include "plain_io.h"
#include "nameclash.h"
#include "buffer.h"
#ifdef USE_XDF
#include "xdf_io.h"
#endif


#include <math.h>

extern int errno;

static void init_geometry_boot(struct bootsector *boot, struct device *dev,
			       int sectors0, int rate_0, int rate_any,
			       int *tot_sectors)
{
	int i;
	int nb_renum;
	int sector2;
	int size2;
	int j;
	int sum;

	set_word(boot->nsect, dev->sectors);
	set_word(boot->nheads, dev->heads);

	*tot_sectors = dev->heads * dev->sectors * dev->tracks - DWORD(nhs);

	if (*tot_sectors < 0x10000){
		set_word(boot->psect, *tot_sectors);
		set_dword(boot->bigsect, 0);
	} else {
		set_word(boot->psect, 0);
		set_dword(boot->bigsect, *tot_sectors);
	}

	if (dev->use_2m & 0x7f){
		strncpy(boot->banner, "2M-STV04", 8);
		boot->res_2m = 0;
		boot->fmt_2mf = 6;
		if ( dev->sectors % ( ((1 << dev->ssize) + 3) >> 2 ))
			boot->wt = 1;
		else
			boot->wt = 0;
		boot->rate_0= rate_0;
		boot->rate_any= rate_any;
		if (boot->rate_any== 2 )
			boot->rate_any= 1;
		i=76;

		/* Infp0 */
		set_word(boot->Infp0, i);
		boot->jump[i++] = sectors0;
		boot->jump[i++] = 108;
		for(j=1; j<= sectors0; j++)
			boot->jump[i++] = j;

		set_word(boot->InfpX, i);
		
		boot->jump[i++] = 64;
		boot->jump[i++] = 3;
		nb_renum = i++;
		sector2 = dev->sectors;
		size2 = dev->ssize;
		j=1;
		while( sector2 ){
			while ( sector2 < (1 << size2) >> 2 )
				size2--;
			boot->jump[i++] = 128 + j;
			boot->jump[i++] = j++;
			boot->jump[i++] = size2;
			sector2 -= (1 << size2) >> 2;
		}
		boot->jump[nb_renum] = ( i - nb_renum - 1 ) / 3;

		set_word(boot->InfTm, i);

		sector2 = dev->sectors;
		size2= dev->ssize;
		while(sector2){
			while ( sector2 < 1 << ( size2 - 2) )
				size2--;
			boot->jump[i++] = size2;
			sector2 -= 1 << (size2 - 2 );
		}
		
		set_word(boot->BootP,i);

		/* checksum */		
		for (sum=0, j=64; j<i; j++) 
			sum += boot->jump[j];/* checksum */
		boot->CheckSum=-sum;
	} else {
		strncpy(boot->banner, "MTOOLS  ", 8);
		set_word(boot->BootP, OFFSET(BootP) + 2);
	}
	return;
}


static int comp_fat_bits(struct Fs_t *Fs, int fat_bits)
{
	if (fat_bits == 0 ){
		if (Fs->num_clus >= FAT12)
			Fs->fat_bits = 16;
		else
			Fs->fat_bits = 12;
	} else if (fat_bits < 0) 
		Fs->fat_bits = -fat_bits;
	else
		Fs->fat_bits = fat_bits;
	return Fs->fat_bits;
}


static inline void format_fat(Fs_t *Fs, struct bootsector *boot)
{
	int buflen;
	
	Fs->fat_error = 0;

	buflen = Fs->fat_len * Fs->sector_size;
	Fs->fat_buf = safe_malloc(buflen);

	memset((char *) Fs->fat_buf, '\0', Fs->sector_size * Fs->fat_len);
	Fs->fat_buf[0] = boot->descr;
	Fs->fat_buf[1] = 0xff;
	Fs->fat_buf[2] = 0xff;
	Fs->fat_dirty = 1;
	if ( Fs->fat_bits == 16 )
		Fs->fat_buf[3] = 0xff;
}


static inline void format_root(Fs_t *Fs, char *label, struct bootsector *boot)
{
	Stream_t *RootDir;
	char *buf;
	int i;
	struct ClashHandling_t ch;

	init_clash_handling(&ch);
	ch.name_converter = label_name;
	ch.ignore_entry = -2;

	buf = safe_malloc(Fs->sector_size);
	RootDir = open_root(COPY((Stream_t *)Fs));
	if(!RootDir){
		fprintf(stderr,"Could not open root directory\n");
		cleanup_and_exit(1);
	}

	memset(buf, '\0', Fs->sector_size);
	WRITES(RootDir, buf, 0, Fs->sector_size);
	for (i = 1; i < Fs->dir_len; i++)
		WRITES(RootDir, buf, i * Fs->sector_size, Fs->sector_size);

	ch.ignore_entry = 1;
	if(label[0])
		mwrite_one(RootDir,(Stream_t *) Fs,
			   "mformat",label, 0, labelit, NULL,&ch);

	FREE(&RootDir);
	set_word(boot->dirents, Fs->dir_len * 16);
}

static void calc_fat_size(Fs_t *Fs, int tot_sectors, int fat_bits)
{
	int rem_sect;
	int tries;
	int occupied;
	
	/* the "remaining sectors" after directory and boot
	 * has been accounted for.
	 */
	rem_sect = tot_sectors - Fs->dir_len - 1;
	
	/* rough estimate of fat size */
	Fs->fat_len = 1;	
	tries=0;
	
	while(1){
		Fs->num_clus = (rem_sect - 2 * Fs->fat_len ) /Fs->cluster_size;
		comp_fat_bits(Fs, fat_bits);
		Fs->fat_len = NEEDED_FAT_SIZE(Fs);
		occupied = 2 * Fs->fat_len + Fs->cluster_size * Fs->num_clus;
		
		/* if we have exactly used up all
		 * sectors, fine */
		if ( occupied == rem_sect )
			break;
		
		/* if we have used up more than we have,
		 * we'll have to reloop */
		
		if ( occupied > rem_sect )
			continue;
		
		/* if we have not used up all our
		 * sectors, try again.  After the second
		 * try, decrease the amount of available
		 * space. This is to deal with the case of
		 * 344 or 345, ..., 1705, ... available
		 * sectors.  */
		
		switch(tries++){
		default:
			/* this should never happen */
			fprintf(stderr,
				"Internal error in cluster/fat repartition"
				" calculation.\n");
				cleanup_and_exit(1);
		case 2:
			/* FALLTHROUGH */
		case 1:
			rem_sect--;
		case 0:
			continue;
		}
	}

	if ( Fs->num_clus >= FAT12 && Fs->fat_bits == 12 ){
		fprintf(stderr,"Too many clusters for this fat size."
			" Please choose a 16-bit fat in your /etc/mtools"
			" or .mtoolsrc file\n");
		cleanup_and_exit(1);
	}
}


static unsigned char bootprog[]=
{0xfa, 0x31, 0xc0, 0x8e, 0xd8, 0x8e, 0xc0, 0xfc, 0xb9, 0x00, 0x01,
 0xbe, 0x00, 0x7c, 0xbf, 0x00, 0x80, 0xf3, 0xa5, 0xea, 0x00, 0x00,
 0x00, 0x08, 0xb8, 0x01, 0x02, 0xbb, 0x00, 0x7c, 0xba, 0x80, 0x00,
 0xb9, 0x01, 0x00, 0xcd, 0x13, 0x72, 0x05, 0xea, 0x00, 0x7c, 0x00,
 0x00, 0xcd, 0x19};

static inline void inst_boot_prg(struct bootsector *boot)
{
	int offset = WORD(BootP);
	memcpy((char *) boot->jump + offset, 
	       (char *) bootprog, sizeof(bootprog) /sizeof(bootprog[0]));
	boot->jump[0] = 0xe9;
	boot->jump[1] = offset + 1;
	boot->jump[2] = 0x90;
	set_word(boot->jump + offset + 20, offset + 24);
}

struct OldDos_t old_dos[]={
{   40,  9,  1, 4, 1, 2, 0xfc },
{   40,  9,  2, 7, 2, 2, 0xfd },
{   40,  8,  1, 4, 1, 1, 0xfe },
{   40,  8,  2, 7, 2, 1, 0xff },
{   80,  9,  2, 7, 2, 3, 0xf9 },
{   80, 15,  2,14, 1, 7, 0xf9 },
{   80, 18,  2,14, 1, 9, 0xf0 },
{   80, 36,  2,15, 2, 9, 0xf0 }
};

static void calc_fs_parameters(struct device *dev,
			       struct Fs_t *Fs, struct bootsector *boot)
{

	int tot_sectors;
	int i;

	/* get the parameters */
	tot_sectors = dev->tracks * dev->heads * dev->sectors - DWORD(nhs);
	for(i=0; i < sizeof(old_dos) / sizeof(old_dos[0]); i++){
		if (dev->sectors == old_dos[i].sectors &&
		    dev->tracks == old_dos[i].tracks &&
		    dev->heads == old_dos[i].heads &&
		    (dev->fat_bits == 0 || dev->fat_bits == 12 || 
		     dev->fat_bits == -12)){
			boot->descr = old_dos[i].media;
			Fs->cluster_size = old_dos[i].cluster_size;
			Fs->dir_len = old_dos[i].dir_len;
			Fs->fat_len = old_dos[i].fat_len;
			Fs->fat_bits = 12;
			break;
		}
	}
	if (i == sizeof(old_dos) / sizeof(old_dos[0]) ){
		/* a non-standard format */
		if(DWORD(nhs))
			boot->descr = 0xf8;
		else
			boot->descr = 0xf0;
		if (dev->heads == 1)
			Fs->cluster_size = 1;
		else {
			Fs->cluster_size = (tot_sectors > 2000 ) ? 1 : 2;
			if (dev->use_2m & 0x7f)
				Fs->cluster_size = 1;
		}

		if (dev->heads == 1)
			Fs->dir_len = 4;
		else
			Fs->dir_len = (tot_sectors > 2000) ? 10 : 7;

		Fs->num_clus = tot_sectors / Fs->cluster_size;
		while (comp_fat_bits(Fs, dev->fat_bits) == 12 && 
		       !(dev->use_2m & 0x7f) && Fs->num_clus >= FAT12){
			Fs->cluster_size <<= 1;
			Fs->num_clus = tot_sectors / Fs->cluster_size;
		}
		
		if ( (Fs->dir_len * 512 ) % Fs->sector_size ){
			Fs->dir_len += Fs->sector_size / 512;
			Fs->dir_len -= Fs->dir_len % (Fs->sector_size / 512);
		}

		calc_fat_size(Fs, tot_sectors, dev->fat_bits);
	}

	set_word(boot->secsiz, Fs->sector_size);
	boot->clsiz = (unsigned char) Fs->cluster_size;

	set_word(boot->fatlen, Fs->fat_len);
	set_word(boot->nsect, dev->sectors);
	set_word(boot->nheads, dev->heads);
}



void mformat(int argc, char **argv, int dummy)
{
	Fs_t Fs;
	int hs;
	int arguse_2m = 0;
	int sectors0=18; /* number of sectors on track 0 */
	int create = 0;
	int rate_0, rate_any;
	int mangled;
	int argssize=0; /* sector size */
	int msize=0;
#ifdef USE_XDF
	int format_xdf = 0;
#endif
	struct bootsector *boot;
	int c, oops;
	struct device used_dev;
	int argtracks, argheads, argsectors;
	int tot_sectors;

	char drive, name[EXPAND_BUF];

	char label[VBUFSIZE], buf[MAX_SECTOR], shortlabel[13];
	struct device *dev;
	char errmsg[80];

	unsigned long serial;
 	int serial_set;

	int Atari = 0; /* should we add an Atari-style serial number ? */
 
	hs = 0;
	oops = 0;
	argtracks = 0;
	argheads = 0;
	argsectors = 0;
	arguse_2m = 0;
	argssize = 0x2;
	label[0] = '\0';
	serial_set = 0;
	serial = 0;

	Fs.refs = 1;
	Fs.Class = &FsClass;
	rate_0 = mtools_rate_0;
	rate_any = mtools_rate_any;

	/* get command line options */
	while ((c = getopt(argc, argv, "CXt:h:s:l:n:H:M:S:12:0Aa")) != EOF) {
		switch (c) {
			case 'C':
				create = O_CREAT;
				break;
			case 'H':
				hs = atoi(optarg);
				break;
#ifdef USE_XDF
			case 'X':
				format_xdf = 1;
				break;
#endif
			case 't':
				argtracks = atoi(optarg);
				break;
			case 'h':
				argheads = atoi(optarg);
				break;
			case 's':
				argsectors = atoi(optarg);
				break;
			case 'l':
				strncpy(label, optarg, VBUFSIZE-1);
				label[VBUFSIZE-1] = '\0';
				break;
 			case 'n':
 				serial = strtoul(optarg,0,0);
 				serial_set = 1;
 				break;
			case 'S':
				argssize = atoi(optarg) | 0x80;
				if(argssize < 0x81)
					oops = 1;
				break;
			case 'M':
				msize = atoi(optarg);
				if (msize % 256 || msize > 8192 )
					oops=1;					
				break;
			case '1':
				arguse_2m = 0x80;
				break;
			case '2':
				arguse_2m = 0xff;
				sectors0 = atoi(optarg);
				break;
			case '0': /* rate on track 0 */
				rate_0 = atoi(optarg);
				break;
			case 'A': /* rate on other tracks */
				rate_any = atoi(optarg);
				break;
			case 'a': /* Atari style serial number */
				Atari = 1;
				break;
			default:
				oops = 1;
				break;
		}
	}

	if (oops || (argc - optind) != 1 ||
	    !argv[optind][0] || argv[optind][1] != ':') {
		fprintf(stderr, 
			"Mtools version %s, dated %s\n", mversion, mdate);
		fprintf(stderr, 
			"Usage: %s [-V] [-t tracks] [-h heads] [-s sectors] "
			"[-l label] [-n serialnumber] "
			"[-S hardsectorsize] [-M softsectorsize] [-1]"
			"[-2 track0sectors] [-0 rate0] [-A rateany] [-a]"
			"device\n", argv[0]);
		cleanup_and_exit(1);
	}

#ifdef USE_XDF
	if(create && format_xdf) {
		fprintf(stderr,"Create and XDF can't be used together\n");
		cleanup_and_exit(1);
	}
#endif
	
	drive = toupper(argv[argc -1][0]);

	/* check out a drive whose letter and parameters match */	
	sprintf(errmsg, "Drive '%c:' not supported", drive);	
	Fs.Direct = NULL;
	for(dev=devices;dev->drive;dev++) {
		FREE(&(Fs.Direct));
		/* drive letter */
		if (dev->drive != drive)
			continue;
		used_dev = *dev;

		set_uint(&used_dev.tracks, argtracks);
		set_uint(&used_dev.heads, argheads);
		set_uint(&used_dev.sectors, argsectors);
		set_uint(&used_dev.use_2m, arguse_2m);
		set_uint(&used_dev.ssize, argssize);
		
		expand(dev->name, name);
#ifdef USE_XDF
		if(!format_xdf)
#endif
			Fs.Direct = SimpleFloppyOpen(&used_dev, dev, name,
						     O_RDWR | create,
						     errmsg);
#ifdef USE_XDF
		else
			Fs.Direct = Fs.Direct = XdfOpen(&used_dev, name, O_RDWR,
							errmsg);
#endif

		if (!Fs.Direct) {
			sprintf(errmsg,"init: open: %s", strerror(errno));
			continue;
		}			

		/* non removable media */
		if (!used_dev.tracks || !used_dev.heads || !used_dev.sectors){
			sprintf(errmsg, 
				"Non-removable media is not supported "
				"(You must tell the complete geometry "
				"of the disk, either in /etc/mtools or "
				"on the command line) ");
			continue;
		}

#if 0
		/* set parameters, if needed */
		if(SET_GEOM(Fs.Direct, &used_dev, 0xf0, boot)){
			sprintf(errmsg,"Can't set disk parameters: %s", 
				strerror(errno));
			continue;
		}
#endif
		Fs.sector_size = 512;
		if( !(used_dev.use_2m & 0x7f))
			Fs.sector_size = 128 << (used_dev.ssize & 0x7f);
		set_uint(&Fs.sector_size, msize);

		/* do a "test" read */
		if (!create &&
		    READS(Fs.Direct, (char *) buf, 0, Fs.sector_size) != 
		    Fs.sector_size) {
			sprintf(errmsg, 
				"Error reading from '%s', wrong parameters?",
				name);
			continue;
		}
		break;
	}


	/* print error msg if needed */	
	if ( dev->drive == 0 ){
		FREE(&Fs.Direct);
		fprintf(stderr,"%s: %s\n", argv[0],errmsg);
		cleanup_and_exit(1);
	}

	/* the boot sector */
	boot = (struct bootsector *) buf;
	memset((char *)boot, '\0', Fs.sector_size);
	set_dword(boot->nhs, hs);

	Fs.Next = buf_init(Fs.Direct,
			   Fs.sector_size * used_dev.heads * used_dev.sectors,
			   Fs.sector_size,
			   Fs.sector_size * used_dev.heads * used_dev.sectors);
	calc_fs_parameters(&used_dev, &Fs, boot);

	set_word(boot->nrsvsect, Fs.fat_start = 1);
	boot->nfat = Fs.num_fat = 2;
	boot->physdrive = 0;
	boot->reserved = 0;
	boot->dos4 = 0x29;
	Fs.dir_start = Fs.num_fat * Fs.fat_len + Fs.fat_start;
	set_word(boot->jump + 510, 0xaa55);
  	if (!serial_set || Atari)
		srandom(time (0));
  	if (!serial_set)
		serial=random();
 	set_dword(boot->serial, serial);	
	if(!label[0])
		strncpy(shortlabel, "NO NAME    ",11);
	else
		label_name(label, 0, &mangled, shortlabel);
	strncpy(boot->label, shortlabel, 11);
	sprintf(boot->fat_type, "FAT%2.2d   ", Fs.fat_bits);
	init_geometry_boot(boot, &used_dev, sectors0, rate_0, rate_any,
			   &tot_sectors);
	if(Atari) {
		boot->banner[4] = 0;
		boot->banner[5] = random();
		boot->banner[6] = random();
		boot->banner[7] = random();
	}		

	if (create) {
		WRITES(Fs.Direct, (char *) buf,
		       Fs.sector_size * (tot_sectors-1),
		       Fs.sector_size);
	}

	inst_boot_prg(boot);
	if(dev->use_2m & 0x7f)
		Fs.num_fat = 1;
	format_fat(&Fs, boot);
	format_root(&Fs, label, boot);
	WRITES((Stream_t *)&Fs, (char *) boot, 0, Fs.sector_size);	
	FLUSH((Stream_t *)&Fs); /* flushes Fs. 
				 * This triggers the writing of the fats */
#ifdef USE_XDF
	if(format_xdf && isatty(0) && !getenv("MTOOLS_USE_XDF"))
		fprintf(stderr,
			"Note:\n"
			"Remember to set the \"MTOOLS_USE_XDF\" environmental\n"
			"variable before accessing this disk\n\n"
			"Bourne shell syntax (sh, ash, bash, ksh, zsh etc):\n"
			" export MTOOLS_USE_XDF=1\n\n"
			"C shell syntax (csh and tcsh):\n"
			" setenv MTOOLS_USE_XDF 1\n" );	
#endif
	cleanup_and_exit(0);
}

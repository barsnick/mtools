#ifndef MTOOLS_FSP_H
#define MTOOLS_FSP_H

#include "stream.h"
#include "msdos.h"
#include "fs.h"

#define NEEDED_FAT_SIZE(x) ((((x)->num_clus+2) * ((x)->fat_bits/4) -1 )/ 2 / (x)->sector_size + 1)

typedef struct Fs_t {
	Class_t *Class;
	int refs;
	Stream_t *Next;
	Stream_t *Buffer;
	
	int serialized;
	unsigned long serial_number;
	int cluster_size;
	unsigned int sector_size;
	int fat_error;


	unsigned int (*fat_decode)(struct Fs_t *This, unsigned int num);
	int (*fat_encode)(struct Fs_t *This, unsigned int num,
			  unsigned int code);

	Stream_t *Direct;
	int fat_dirty;
	int fat_start;
	int fat_len;

	int num_fat;
	int end_fat;
	int last_fat;
	int fat_bits;
	unsigned char *fat_buf;

	int dir_start;
	int dir_len;
	int num_clus;
	char drive; /* for error messages */
} Fs_t;

unsigned int get_next_free_cluster(Fs_t *Fs, unsigned int last);
int fat_read(Fs_t *This, struct bootsector *boot, int fat_bits);
void fat_write(Fs_t *This);
extern Class_t FsClass;
#endif

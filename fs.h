#ifndef MTOOLS_FS_H
#define MTOOLS_FS_H

#include "stream.h"


typedef struct FsPublic_t {
	Class_t *Class;
	int refs;
	Stream_t *Next;
	Stream_t *Buffer;

	int serialized;
	unsigned long serial_number;
	int cluster_size;
	int sector_size;
	int fat_error;
} FsPublic_t;

Stream_t *fs_init(char drive, int mode);
int fat_free(Stream_t *Fs, unsigned int fat);

#endif

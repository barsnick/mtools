/*
 * mbadblocks.c
 * Mark bad blocks on disk
 *
 */

#include "sysincludes.h"
#include "msdos.h"
#include "mtools.h"
#include "streamcache.h"
#include "fsP.h"

void mbadblocks(int argc, char **argv, int type)
{
	int i;
	char *in_buf;
	int in_len;
	long start;
	struct StreamCache_t sc;
	Fs_t *Fs;
	Stream_t *Dir;
	int ret;

	if (argc != 2) {
		fprintf(stderr, "Mtools version %s, dated %s\n", 
			mversion, mdate);
		fprintf(stderr, "Usage: %s [-V] device\n", argv[0]);
		cleanup_and_exit(1);
	}

	init_sc(&sc);

	Dir = open_subdir(&sc, argv[1], O_RDWR, (Stream_t **) &Fs);

	if (!Fs) {
		fprintf(stderr,"%s: Cannot initialize drive\n", argv[0]);
		cleanup_and_exit(1);
	}

	in_len = Fs->cluster_size * Fs->sector_size;
	in_buf = safe_malloc(in_len);
	for(i=0; i < Fs->dir_start + Fs->dir_len; i++ ){
		ret = READS(Fs->Next, in_buf, i * Fs->sector_size, 
			   Fs->sector_size);
		if( ret < 0 ){
			perror("early error");
			cleanup_and_exit(1);
		}
		if(ret < Fs->sector_size){
			fprintf(stderr,"end of file in file_read\n");
			cleanup_and_exit(1);
		}
	}
		
	in_len = Fs->cluster_size * Fs->sector_size;
	for(i=2; i< Fs->num_clus + 2; i++){
		if(Fs->fat_decode((Fs_t*)Fs,i))
			continue;
		start = (i - 2) * Fs->cluster_size + 
			Fs->dir_start + Fs->dir_len;
		ret = READS(Fs->Next, in_buf, start * Fs->sector_size, in_len);
		if(ret < in_len ){
			printf("Bad cluster %d found\n", i);
			Fs->fat_encode((Fs_t*)Fs, i, 0xfff7);
			continue;
		}
	}
	FREE(&Dir);
	finish_sc(&sc);
	cleanup_and_exit(0);
}

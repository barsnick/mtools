#include "sysincludes.h"
#include "msdos.h"
#include "fsP.h"

/*
 * Remove a string of FAT entries (delete the file).  The argument is
 * the beginning of the string.  Does not consider the file length, so
 * if FAT is corrupted, watch out!
 */

int fat_free(Stream_t *Stream, unsigned int fat)
{
	DeclareThis(Fs_t);
	unsigned int next;
					/* a zero length file? */
	if (fat == 0)
		return(0);

	/* CONSTCOND */
	while (1) {
					/* get next cluster number */
		next = This->fat_decode(This,fat);
		/* mark current cluster as empty */
		if (This->fat_encode(This,fat, 0) || next == 1) {
			fprintf(stderr, "fat_free: FAT problem\n");
			This->fat_error++;
			return(-1);
		}
		if (next >= This->last_fat)
			break;
		fat = next;
	}
	return(0);
}

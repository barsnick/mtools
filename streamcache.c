/*
 * streamcache.c
 * Managing a cache of open disks
 */

#include "sysincludes.h"
#include "msdos.h"
#include "mtools.h"
#include "vfat.h"
#include "fs.h"
#include "streamcache.h"
#include "plain_io.h"

Stream_t *open_subdir(StreamCache_t *sc, char *arg, int flags, Stream_t **Fsp)
{
	int drive;
	Stream_t *Fs;
	char pathname[MAX_PATH];

	sc->drivename = drive = get_drive(arg, *(sc->mcwd));
	
	/* open the drive */
	if(sc->fss[drive])
		Fs = sc->fss[drive];
	else {
		Fs = fs_init(drive, flags);
		if (!Fs){
			fprintf(stderr, "Cannot initialize '%c:'\n", drive);
			return NULL;
		}

		sc->fss[drive] = Fs;
	}

	if(Fsp)
		*Fsp = Fs;

	get_name(arg, sc->filename, sc->mcwd);
	get_path(arg, pathname, sc->mcwd);
	if(sc->pathname)
		strcpy(sc->pathname, pathname);

	if (sc->last_drive != drive || 
	    strcasecmp(pathname,sc->subdir_name)){
		FREE(&sc->subdir);
		
		sc->subdir = subdir(Fs, pathname);
		sc->last_drive = drive;
		strcpy(sc->subdir_name, pathname);
	}	
	return COPY(sc->subdir);
}

void init_sc(StreamCache_t *sc)
{
	int i;

	sc->last_drive = '\0';
	fix_mcwd(sc->mcwd);

	for(i=0; i<256; i++)
		sc->fss[i]=0;

	sc->subdir= NULL;
	sc->subdir_name[0]='\0';
	sc->unixcallback = NULL;
	sc->newdoscallback = NULL;
	sc->pathname = sc->outname = sc->shortname = sc->longname = 0;
	sc->newdrive_cb = sc->olddrive_cb = 0;
}


void finish_sc(StreamCache_t *sc)
{
	int i;

	FREE(&sc->subdir);
	for(i=0; i<256; i++){
		if(sc->fss[i] && sc->fss[i]->refs != 1 )
			fprintf(stderr,"Streamcache allocation problem:%c %d\n",
				i, sc->fss[i]->refs);
		FREE(&(sc->fss[i]));
	}
}


int unix_loop(StreamCache_t *sc, char *arg)
{
	char *tmp;
	int ret;

	sc->File = NULL;
	if((sc->lookupflags & DO_OPEN)){
		sc->File = SimpleFloppyOpen(0, 0, arg, O_RDONLY, 0);
		if(!sc->File){
			perror(arg);
#if 0
			tmp = strrchr(arg,'/');
			if(tmp)
				tmp++;
			else
				tmp = arg;
			strncpy(sc->filename, tmp, VBUFSIZE);
			sc->filename[VBUFSIZE-1] = '\0';
#endif
			return MISSED_ONE;
		}
	}

	if(sc->outname){
		tmp = strrchr(arg,'/');
		if(tmp)
			tmp++;
		else
			tmp = arg;
		strncpy(sc->outname, tmp, VBUFSIZE-1);
		sc->outname[VBUFSIZE-1]='\0';
	}
	ret = sc->unixcallback(arg,sc) | IS_MATCH;
	FREE(&sc->File);
	return ret;
}

int dos_loop(StreamCache_t *sc, char *arg)
{
	Stream_t *Dir;
	int ret;
	int have_one;
	int entry;
	Stream_t *Fs;
	char *filename;

	have_one = MISSED_ONE;
	ret = 0;
	Dir =  open_subdir(sc, arg, sc->openflags, &Fs);
	if(!Dir)
		return MISSED_ONE;
	
	entry = 0;
	filename = sc->filename;
	if(!filename[0])
	    filename="*";
	while(entry >= 0){
		if(got_signal)
			break;
		sc->File = NULL;
		if(vfat_lookup(Dir, Fs,
			       & (sc->dir), &entry, 0,
			       filename,
			       sc->lookupflags,
			       sc->outname,
			       sc->shortname,
			       sc->longname,
			       &sc->File) == 0 ){
			if(got_signal)
				break;
			have_one = IS_MATCH;
			sc->Fs = Fs;
			ret |= sc->callback(Dir, sc, entry - 1);
			FREE(&sc->File);
		}		
		if (((FsPublic_t *) Fs)->fat_error)
			ret |= MISSED_ONE;
		if(ret & NEXT_DISK)
			break;
		
	}
	FREE(&Dir);
	return ret | have_one;
}

int main_loop(StreamCache_t *sc, char *progname, char **argv, int argc)
{
	int i;
	int ret, Bret;
	
	Bret = 0;

	for (i = 0; i < argc; i++) {
		if ( got_signal )
			break;
		if (sc->unixcallback && (!argv[i][0] || argv[i][1] != ':' ))
			ret = unix_loop(sc, argv[i]);
		else if (sc->newdoscallback)
			ret = sc->newdoscallback(argv[i], sc) ;
		else
			ret = dos_loop(sc, argv[i]);
		
		if (! (ret & IS_MATCH) ) {
			fprintf(stderr, "%s: File \"%s\" not found\n",
				progname, argv[i]);
			ret |= MISSED_ONE;
		}
		Bret |= ret;
	}
	if ((Bret & GOT_ONE) && ( Bret & MISSED_ONE))
		return 2;
	if (Bret & MISSED_ONE)
		return 1;
	return 0;
}


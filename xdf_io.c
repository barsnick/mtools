/*
 * Io to an xdf disk
 *
 * written by:
 *
 * Alain L. Knaff			
 * Alain.Knaff@inrialpes.fr
 *
 */


#include "sysincludes.h"
#ifdef linux
#include "msdos.h"
#include "mtools.h"
#include "devices.h"
#include "xdf_io.h"
#include "patchlevel.h"

extern int errno;

typedef struct sector_map {
	int head;
	int sector;
	int size;
	int bytes;
	int phantom;
} sector_map_t;

sector_map_t generic_map[]={
	/* Algorithms can't be patented */
	{ 0, 131, 3,1024, 0 },
	{ 0, 132, 4,2048, 0 },
	{ 1, 134, 6,8192, 0 },
	{ 0, 130, 2, 512, 0 },
	{ 1, 130, 2, 512, 0 },
	{ 0, 134, 6,8192, 0 },
	{ 1, 132, 4,2048, 0 },
	{ 1, 131, 3,1024, 0 },
	{ 0,   0, 0,   0, 0 }
};

sector_map_t zero_map[]={
	{ 0, 129, 2, 11*512, 0 },
	{ 1, 129, 2,  1*512, 0 },
	{ 0,   1, 2,  8*512, 0 },
	{ 0,   0, 2,  3*512, 1 },
	{ 1, 130, 2, 14*512, 0 },
	{ 0,   4, 2,  5*512, 2 },
	{ 1, 144, 2,  4*512, 0 },
	{ 0,   0, 0,      0, 0 }
};


typedef struct {
	unsigned char begin; /* where it begins */
	unsigned char end;       
	unsigned char sector;
	unsigned char sizecode;

	unsigned int dirty:1;
	unsigned int phantom:2;
	unsigned int valid:1;
	unsigned int head:1;
} TrackMap_t;



typedef struct Xdf_t {
	Class_t *Class;
	int refs;
	Stream_t *Next;
	Stream_t *Buffer;

	int fd;
	char *buffer;
	
	int current_track;
	
	sector_map_t *zero_map;
	sector_map_t *generic_map;

	int track_size;
	int sector_size;
	TrackMap_t *track_map;

	unsigned char last_sector;

	unsigned int stretch:1;
	unsigned int rate:2;
	signed  int drive:4;
} Xdf_t;

typedef struct {
	unsigned char head;
	unsigned char sector;
} Compactify_t;


static int analyze_reply(RawRequest_t *raw_cmd, int do_print)
{
	int ret, bytes, newbytes;

	bytes = 0;
	while(1) {
		ret = analyze_one_reply(raw_cmd, &newbytes, do_print);
		bytes += newbytes;
		switch(ret) {
			case 0:
				return bytes;
			case 1:
				raw_cmd++;
				break;
			case -1:
				if(bytes)
					return bytes;
				else
					return 0;
		}
	}
}
				


static int send_cmd(int fd, RawRequest_t *raw_cmd, int nr,
		    char *message, int retries)
{
	int j;
	int ret=-1;
	
	if(!nr)
		return 0;
	for (j=0; j< retries; j++){
		switch(send_one_cmd(fd, raw_cmd, message)) {
			case -1:
				return -1;
			case 1:
				j++;
				continue;
			case 0:
				break;
		}
		if((ret=analyze_reply(raw_cmd, j)) > 0)
			return ret; /* ok */
	}
	if(j > 1 && j == retries) {
		fprintf(stderr,"Too many errors, giving up\n");
		return 0;
	}
	return -1;
}



#define REC (This->track_map[ptr])
#define END(x) (This->track_map[(x)].end)
#define BEGIN(x) (This->track_map[(x)].begin)

static int add_to_request(Xdf_t *This, int ptr,
			  RawRequest_t *request, int *nr,
			  int direction, Compactify_t *compactify)
{
#if 0
	if(direction == MT_WRITE) {
		printf("writing %d: %d %d %d %d [%02x]\n", 
		       ptr, This->current_track,
		       REC.head, REC.sector, REC.sizecode,
		       *(This->buffer + ptr * This->sector_size));
	} else
			printf(" load %d.%d\n", This->current_track, ptr);
#endif
	if(REC.phantom && direction== MT_WRITE)
		return 0;
	if(REC.phantom == 1) {
		memset(This->buffer + ptr * This->sector_size, 0,
		       128 << REC.sizecode);
		return 0;
	}

	
	if(*nr &&
	   RR_SIZECODE(request+(*nr)-1) == REC.sizecode &&	   
	   compactify->head == REC.head && 
	   compactify->sector +1 == REC.sector) {
		RR_SETSIZECODE(request+(*nr)-1, REC.sizecode);
	} else {
		if(*nr)
			RR_SETCONT(request+(*nr)-1);
		RR_INIT(request+(*nr));
		RR_SETDRIVE(request+(*nr), This->drive);
		RR_SETRATE(request+(*nr), This->rate);
		RR_SETTRACK(request+(*nr), This->current_track);
		RR_SETPTRACK(request+(*nr), 
			     This->current_track << This->stretch);
		RR_SETHEAD(request+(*nr), REC.head);
		RR_SETSECTOR(request+(*nr), REC.sector);
		RR_SETSIZECODE(request+(*nr), REC.sizecode);
		RR_SETDIRECTION(request+(*nr), direction);
		RR_SETDATA(request+(*nr),
			   (caddr_t) This->buffer + ptr * This->sector_size);
		(*nr)++;
	}
	compactify->head = REC.head;
	compactify->sector = REC.sector;
	return 0;
}


static void add_to_request_if_invalid(Xdf_t *This, int ptr,
				     RawRequest_t *request, int *nr,
				     Compactify_t *compactify)
{
	if(!REC.valid)
		add_to_request(This, ptr, request, nr, MT_READ, compactify);

}


static void adjust_bounds(Xdf_t *This, int *begin, int *end)
{
	/* translates begin and end from byte to sectors */
	*begin = *begin / This->sector_size;
	*end = (*end + This->sector_size - 1) / This->sector_size;
}


static inline int try_flush_dirty(Xdf_t *This)
{
	int ptr, nr, bytes;
	RawRequest_t requests[100];
	Compactify_t compactify;

	if(This->current_track < 0)
		return 0;
	
	nr = 0;
	for(ptr=0; ptr < This->last_sector; ptr=REC.end)
		if(REC.dirty)
			add_to_request(This, ptr,
				       requests, &nr,
				       MT_WRITE, &compactify);
#if 1
	bytes = send_cmd(This->fd,requests, nr, "writing", 4);
	if(bytes < 0)
		return bytes;
#else
	bytes = 0xffffff;
#endif
	for(ptr=0; ptr < This->last_sector; ptr=REC.end)
		if(REC.dirty) {
			if(bytes >= REC.end - REC.begin) {
				bytes -= REC.end - REC.begin;
				REC.dirty = 0;
			} else
				return 1;
		}
	return 0;
}



static int flush_dirty(Xdf_t *This)
{	
	int ret;

	while((ret = try_flush_dirty(This))) {
		if(ret < 0)		       
			return ret;
	}
	return 0;
}


static int load_data(Xdf_t *This, int begin, int end, int retries)
{
	int ptr, nr, bytes;
	RawRequest_t requests[100];
	Compactify_t compactify;

	adjust_bounds(This, &begin, &end);
	
	ptr = begin;
	nr = 0;
	for(ptr=REC.begin; ptr < end ; ptr = REC.end)
		add_to_request_if_invalid(This, ptr, requests, &nr,
					  &compactify);
	bytes = send_cmd(This->fd,requests, nr, "reading", retries);
	if(bytes < 0)
		return bytes;
	ptr = begin;
	for(ptr=REC.begin; ptr < end ; ptr = REC.end) {
		if(!REC.valid) {
			if(bytes >= REC.end - REC.begin) {
				bytes -= REC.end - REC.begin;
				REC.valid = 1;
			} else if(ptr > begin)
				return ptr * This->sector_size;
			else
				return -1;
		}
	}
	return end * This->sector_size;
}

static void mark_dirty(Xdf_t *This, int begin, int end)
{
	int ptr;

	adjust_bounds(This, &begin, &end);
	
	ptr = begin;
	for(ptr=REC.begin; ptr < end ; ptr = REC.end) {
		REC.valid = 1;
		if(!REC.phantom)
			REC.dirty = 1;
	}
}


static int load_bounds(Xdf_t *This, int begin, int end)
{
	int lbegin, lend;
	int endp1, endp2;

	lbegin = begin;
	lend = end;

	adjust_bounds(This, &lbegin, &lend);	

	if(begin != BEGIN(lbegin) * This->sector_size &&
	   end != BEGIN(lend) * This->sector_size &&
	   lend < END(END(lbegin)))
		/* contiguous end & begin, load them in one go */
		return load_data(This, begin, end, 4);

	if(begin != BEGIN(lbegin) * This->sector_size) {
		endp1 = load_data(This, begin, begin, 4);
		if(endp1 < 0)
			return endp1;
	}

	if(end != BEGIN(lend) * This->sector_size) {
		endp2 = load_data(This, end, end, 4);
		if(endp2 < 0)
			return BEGIN(lend) * This->sector_size;
	}
	return lend * This->sector_size;
}


static void decompose(Xdf_t *This, int where, int len, int *begin, int *end)
{
	int i;
	int ptr, track;
	sector_map_t *map;
	int lbegin, lend;

	
	track = where / This->track_size;
	
	*begin = where - track * This->track_size;
	*end = where + len - track * This->track_size;
	maximize(end, This->track_size);

	if(This->current_track == track)
		/* already OK, return immediately */
		return;
	flush_dirty(This);
	This->current_track = track;

	if(track)
		map = generic_map;
	else
		map = zero_map;
	
	for(ptr=0; map->bytes ; map++) {
		/* iterate through all sectors */
		for(i=0; i < map->bytes >> (map->size + 7); i++) {
			lbegin = ptr;
			lend = ptr + (128 << map->size) / This->sector_size;
			for( ; ptr < lend ; ptr++) {
				REC.begin = lbegin;
				REC.end = lend;
				
				REC.head = map->head;
				REC.sector = map->sector + i;
				REC.sizecode = map->size;

				REC.valid = 0;
				REC.dirty = 0;
				REC.phantom = map->phantom;
			}
		}
	}
	REC.begin = REC.end = ptr;
	This->last_sector = ptr;
}


static int xdf_read(Stream_t *Stream, char *buf, int where, int len)
{	
	int begin, end, len2;
	DeclareThis(Xdf_t);

	decompose(This, where, len, &begin, &end);
	len2 = load_data(This, begin, end, 4);
	if(len2 < 0)
		return len2;
	len2 -= begin;
	maximize(&len, len2);
	memcpy(buf, This->buffer + begin, len);
	return end - begin;
}

static int xdf_write(Stream_t *Stream, char *buf, int where, int len)
{	
	int begin, end, len2;
	DeclareThis(Xdf_t);

	decompose(This, where, len, &begin, &end);
	len2 = load_bounds(This, begin, end);
	if(len2 < 0)
		return len2;
	maximize(&end, len2);
	len2 -= begin;
	maximize(&len, len2);
	memcpy(This->buffer + begin, buf, len);
	mark_dirty(This, begin, end);
	return end - begin;
}

static int xdf_flush(Stream_t *Stream)
{
	DeclareThis(Xdf_t);

	return flush_dirty(This);       
}

static int xdf_free(Stream_t *Stream)
{
	DeclareThis(Xdf_t);
	Free(This->track_map);
	Free(This->buffer);
	return close(This->fd);
}


static int check_geom(struct device *dev, int media, struct bootsector *boot)
{
	if(media >= 0xfc && media <= 0xff)
		return 1; /* old DOS */
	
	/* check against contradictory info from configuration file */
	if(compare(dev->tracks, 80) ||
	   compare(dev->sectors, 23) ||
	   compare(dev->heads, 2))
		return 1;

	/* check against info from boot */
	if(boot && 
	   (WORD(nsect) != 23 || WORD(psect) != 3680 || WORD(nheads) !=2))
		return 1;
	return 0;
}

static void set_geom(struct device *dev)
{
	/* fill in config info to be returned to user */
	dev->tracks = 80;
	dev->sectors = 23;
	dev->heads = 2;
	dev->use_2m = 0xff;
}

static int config_geom(Stream_t *Stream, struct device *dev, 
		       struct device *orig_dev, int media,
		       struct bootsector *boot)
{
	if(check_geom(dev, media, boot))
		return 1;
	set_geom(dev);    
	return 0;
}

static Class_t XdfClass = {
	xdf_read, 
	xdf_write, 
	xdf_flush, 
	xdf_free, 
	config_geom, 
	0 /* get_data */
};

Stream_t *XdfOpen(struct device *dev, char *name,
		  int mode, char *errmsg)
{
	Xdf_t *This;
	int begin, end;

	if(dev && (!dev->use_xdf || check_geom(dev, 0, 0)))
		return NULL;

	This = New(Xdf_t);
	if (!This)
		return NULL;

	This->Class = &XdfClass;
	This->track_size = 46 * 512;
	This->sector_size = 512;


	This->zero_map = zero_map;
	This->generic_map = generic_map;
	This->stretch = 0;
	This->rate = 0;

	This->fd = open(name, mode | dev->mode | O_EXCL | O_NDELAY);
	if(This->fd < 0) {
		sprintf(errmsg,"xdf floppy: open: \"%s\"", strerror(errno));
		goto exit_0;
	}

	This->drive = GET_DRIVE(This->fd);
	if(This->drive < 0)
		goto exit_1;

	/* allocate buffer */
	This->buffer = (char *) malloc(46 * 512);
	if (!This->buffer)
		goto exit_1;

	This->current_track = -1;
	This->track_map = (TrackMap_t *)
		calloc(This->track_size / This->sector_size + 1,
		       sizeof(TrackMap_t));
	if(!This->track_map)
		goto exit_2;

	/* lock the device on writes */
	if (mode == O_RDWR && lock_dev(This->fd)) {
		sprintf(errmsg,"xdf floppy: device \"%s\"busy\n:", 
			dev->name);
		goto exit_3;
	}
	
	decompose(This, 0, 512, &begin, &end);
	if (load_data(This, 0, 1, 1) < 0 )
		goto exit_3;
	
	This->refs = 1;
	This->Next = 0;
	This->Buffer = 0;
	if(dev)
		set_geom(dev);
	return (Stream_t *) This;

exit_3:
	Free(This->track_map);
exit_2:
	Free(This->buffer);
exit_1:
	close(This->fd);
exit_0:
	Free(This);
	return NULL;
}

#endif

/* Algorithms can't be patented */


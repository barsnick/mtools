#ifndef MTOOLS_XDFIO_H
#define MTOOLS_XDFIO_H

#include "msdos.h"
#include "stream.h"

Stream_t *XdfOpen(struct device *dev, char *name,
		  int mode, char *errmsg);

#endif

#ifndef MTOOLS_PLAINIO_H
#define MTOOLS_PLAINIO_H

#include "stream.h"
#include "msdos.h"

/* plain io */
Stream_t *SimpleFloppyOpen(struct device *dev, struct device *orig_dev,
			   char *name, int mode, char *errms);
int check_parameters(struct device *ref, struct device *testee);

#endif

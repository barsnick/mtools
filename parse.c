#include "sysincludes.h"
#include "msdos.h"
#include "vfat.h"

/*
 * Get name component of filename.  Returns pointer to static area.
 *
 * Formerly translated name to upper case; now preserves case.
 * This is because long DOS names are case sensitive, but also because we
 * want to preserve the user-specified case for the copied Unix files
 */

char *get_name(char *filename, char *ans, char *mcwd)
{
	char *s, *temp;

	temp = filename;
					/* skip drive letter */
	if (temp[0] && temp[1] == ':')
		temp = &temp[2];
					/* find the last separator */
	if ((s = strrchr(temp, '/')))
		temp = s + 1;
	if ((s = strrchr(temp, '\\')))
		temp = s + 1;

	strncpy(ans, temp, VBUFSIZE-1);
	ans[VBUFSIZE-1]='\0';

	return ans;
}

/*
 * Get the path component of the filename.
 * Doesn't alter leading separator, always strips trailing separator (unless
 * it is the path itself).
 *
 * Formerly translated name to upper case; now preserves case.
 * This is because long DOS names are case sensitive, but also because we
 * want to preserve the user-specified case for the copied Unix files
 */

char *get_path(char *filename, char *ans, char *mcwd)
{
	char *s, *end, *begin;
	char drive;
	int has_sep;

	begin = filename;

	/* skip drive letter */
	drive = '\0';
	if (begin[0] && begin[1] == ':') {
		drive = toupper(begin[0]);
		begin += 2;
	}

	/* if absolute path */
	if (*begin == '/' || *begin == '\\')
		ans[0] = '\0';
	else {
		if (!drive || drive == *mcwd)
			strcpy(ans, mcwd + 2);
		else
			strcpy(ans, "/");
	}

	/* find last separator */
	has_sep = 0;
	end = begin;
	if ((s = strrchr(end, '/'))) {
		has_sep++;
		end = s;
	}
	if ((s = strrchr(end, '\\'))) {
		has_sep++;
		end = s;
	}

	if(strlen(ans)+end-begin+1 > MAX_PATH){
		fprintf(stderr,"Path too long");
		cleanup_and_exit(1);
	}

	strncat(ans, begin, end - begin);
	ans[strlen(ans)+end-begin] = '\0';

	/* if separator alone, put it back */
	if (begin == end && has_sep)
		strcat(ans, "/");

	return ans;
}

/*
 * get the drive letter designation
 */

char get_drive(char *filename, char def)
{
	if (*filename && *(filename + 1) == ':')
		return(toupper(*filename));
	else
		return def;
}

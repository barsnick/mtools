/*
 * Do shell-style pattern matching for '?', '\', '[..]', and '*' wildcards.
 * Returns 1 if match, 0 if not.
 */

#include "sysincludes.h"
#include "mtools.h"


int casecmp(char a,char b)
{
	return toupper(a) == toupper(b);
}

int exactcmp(char a,char b)
{
	return a == b;
}



int match(__const char *s, __const char *p, char *out, int Case)
{
	int matched, reverse;
	char first, last;

	int (*compfn)(char a, char b);

	if(Case)
		compfn = casecmp;
	else
		/*compfn = exactcmp;*/
		compfn = casecmp;

	for (; *p != '\0'; ) {
		switch (*p) {
			case '?':	/* match any one character */
				if (*s == '\0')
					return(0);
				if(out)
					*(out++) = *p;
				break;
			case '*':	/* match everything */
				while (*p == '*')
					p++;


					/* search for next char in pattern */
				matched = 0;
				while (*s != '\0') {
					if(out)
						*(out++) = *s;
					if (compfn(*s,*p)) {
						matched = 1;
						break;
					}
					s++;
				}
				/* if last char in pattern */
				if (*p == '\0')
					continue;

				if (!matched)
					return(0);
				break;
			case '[':	 /* match range of characters */
				first = '\0';
				matched = 0;
				reverse = 0;
				while (*++p != ']') {
					if (*p == '^') {
						reverse = 1;
						p++;
					}
					first = *p;
					if (first == ']' || first == '\0')
						return(0);

					/* if 2nd char is '-' */
					if (*(p + 1) == '-') {
						p++;
					/* set last to 3rd char ... */
						last = *++p;
						if (last == ']' || last == '\0')
							return(0);
					/* test the range of values */
						if (*s >= first &&
						    *s <= last) {
							if(out)
								*(out++) = *s;
							matched = 1;
							p++;
							break;
						}
						if(Case &&
						   toupper(*s) >= first &&
						   toupper(*s) <= last){
							if(out)
								*(out++) = toupper(*s);
							matched = 1;
							p++;
							break;
						}
						if(Case &&
						   tolower(*s) >= first &&
						   tolower(*s) <= last){
							if(out)
								*(out++) = tolower(*s);
							matched = 1;
							p++;
							break;
						}
						return(0);
					}
					if (compfn(*s,*p)){
						if(out)
							*(out++) = *p;
						matched = 1;
					}
				}
				if (matched && reverse)
					return(0);
				if (!matched)
					return(0);
				break;
			case '\\':	/* Literal match with next character */
				p++;
				/* fall thru */
			default:
				if (!compfn(*s,*p))
					return(0);
				if(out)
					*(out++) = *p;
				break;
		}
		p++;
		s++;
	}
	if(out)
		*out = '\0';

					/* string ended prematurely ? */
	if (*s != '\0')
		return(0);
	else
		return(1);
}

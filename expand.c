/*
 * Do filename expansion with the shell.
 */

#define EXPAND_BUF	2048

#include "sysincludes.h"

char *expand(char *input, char *ans)
{
	FILE *fp;
	int last;
	char buf[256];

	if (input == NULL)
		return(NULL);
	if (*input == '\0')
		return("");
					/* any thing to expand? */
	if (!strpbrk(input, "$*{}[]\\?~")) {
		strcpy(ans, input);
		return(ans);
	}
					/* popen an echo */
	sprintf(buf, "echo %s", input);

	fp = popen(buf, "r");
	fgets(ans, EXPAND_BUF, fp);
	pclose(fp);

	if (!strlen(ans)) {
		strcpy(ans, input);
		return(ans);
	}

	/*
	 * A horrible kludge...  if the last character is not a line feed,
	 * then the csh has returned an error message.  Otherwise zap the
	 * line feed.
	 */
	last = strlen(ans) - 1;
	if (ans[last] != '\n') {
		strcpy(ans, input);
		return(ans);
	}
	else
		ans[last] = '\0';

	return(ans);
}

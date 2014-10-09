// <poll.h> might need this for sigset_t
#define _XOPEN_SOURCE 600

#include <poll.h>
#include <stdio.h>
#include <unistd.h>
#include <locale.h>

#include "termkey2.h"

static void
on_key (termkey_t *tk, termkey_key_t *key)
{
	char buffer[50];
	termkey_strfkey (tk, buffer, sizeof buffer, key, TERMKEY_FORMAT_VIM);
	printf ("%s\n", buffer);
}

int
main (int argc, char *argv[])
{
	(void) argc;
	(void) argv;

	TERMKEY_CHECK_VERSION;
	setlocale (LC_CTYPE, "");

	termkey_t *tk = termkey_new (STDIN_FILENO, NULL, 0);

	if (!tk)
	{
		fprintf (stderr, "Cannot allocate termkey instance\n");
		exit (1);
	}

	struct pollfd fd;
	fd.fd = STDIN_FILENO; /* the file descriptor we passed to termkey_new() */
	fd.events = POLLIN;

	termkey_result_t ret;
	termkey_key_t key;

	int running = 1;
	int nextwait = -1;

	while (running)
	{
		if (poll (&fd, 1, nextwait) == 0)
			// Timed out
			if (termkey_getkey_force (tk, &key) == TERMKEY_RES_KEY)
				on_key (tk, &key);

		if (fd.revents & (POLLIN | POLLHUP | POLLERR))
			termkey_advisereadable (tk);

		while ((ret = termkey_getkey (tk, &key)) == TERMKEY_RES_KEY)
		{
			on_key (tk, &key);

			if (key.type == TERMKEY_TYPE_KEY
			 && (key.modifiers & TERMKEY_KEYMOD_CTRL)
			 && (key.code.codepoint == 'C' || key.code.codepoint == 'c'))
				running = 0;
		}

		if (ret == TERMKEY_RES_AGAIN)
			nextwait = termkey_get_waittime (tk);
		else
			nextwait = -1;
	}

	termkey_destroy (tk);
}

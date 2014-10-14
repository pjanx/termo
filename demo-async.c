// <poll.h> might need this for sigset_t
#define _XOPEN_SOURCE 600

#include <poll.h>
#include <stdio.h>
#include <unistd.h>
#include <locale.h>

#include "termo.h"

static void
on_key (termo_t *tk, termo_key_t *key)
{
	char buffer[50];
	termo_strfkey (tk, buffer, sizeof buffer, key, TERMO_FORMAT_VIM);
	printf ("%s\n", buffer);
}

int
main (int argc, char *argv[])
{
	(void) argc;
	(void) argv;

	TERMO_CHECK_VERSION;
	setlocale (LC_CTYPE, "");

	termo_t *tk = termo_new (STDIN_FILENO, NULL, 0);

	if (!tk)
	{
		fprintf (stderr, "Cannot allocate termo instance\n");
		exit (1);
	}

	struct pollfd fd;
	fd.fd = STDIN_FILENO; // the file descriptor we passed to termo_new()
	fd.events = POLLIN;

	termo_result_t ret;
	termo_key_t key;

	int running = 1;
	int nextwait = -1;

	while (running)
	{
		if (poll (&fd, 1, nextwait) == 0)
			// Timed out
			if (termo_getkey_force (tk, &key) == TERMO_RES_KEY)
				on_key (tk, &key);

		if (fd.revents & (POLLIN | POLLHUP | POLLERR))
			termo_advisereadable (tk);

		while ((ret = termo_getkey (tk, &key)) == TERMO_RES_KEY)
		{
			on_key (tk, &key);

			if (key.type == TERMO_TYPE_KEY
			 && (key.modifiers & TERMO_KEYMOD_CTRL)
			 && (key.code.codepoint == 'C' || key.code.codepoint == 'c'))
				running = 0;
		}

		if (ret == TERMO_RES_AGAIN)
			nextwait = termo_get_waittime (tk);
		else
			nextwait = -1;
	}

	termo_destroy (tk);
}

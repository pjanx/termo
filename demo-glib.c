#include <stdio.h>
#include <glib.h>
#include <unistd.h>
#include <locale.h>

#include "termo.h"

static termo_t *tk;
static int timeout_id;

static void
on_key (termo_t *tk, termo_key_t *key)
{
	char buffer[50];
	termo_strfkey (tk, buffer, sizeof buffer, key, TERMO_FORMAT_VIM);
	printf ("%s\n", buffer);
}

static gboolean
key_timer (gpointer data)
{
	termo_key_t key;
	if (termo_getkey_force (tk, &key) == TERMO_RES_KEY)
		on_key (tk, &key);
	return FALSE;
}

static gboolean
stdin_io (GIOChannel *source, GIOCondition condition, gpointer data)
{
	if (condition & G_IO_IN)
	{
		if (timeout_id)
			g_source_remove (timeout_id);

		termo_advisereadable (tk);

		termo_result_t ret;
		termo_key_t key;
		while ((ret = termo_getkey (tk, &key)) == TERMO_RES_KEY)
			on_key (tk, &key);

		if (ret == TERMO_RES_AGAIN)
			timeout_id = g_timeout_add
				(termo_get_waittime (tk), key_timer, NULL);
	}

	return TRUE;
}

int
main (int argc, char *argv[])
{
	(void) argc;
	(void) argv;

	TERMO_CHECK_VERSION;
	setlocale (LC_CTYPE, "");

	tk = termo_new (STDIN_FILENO, NULL, 0);
	if (!tk)
	{
		fprintf (stderr, "Cannot allocate termo instance\n");
		exit (1);
	}

	GMainLoop *loop = g_main_loop_new (NULL, FALSE);
	g_io_add_watch (g_io_channel_unix_new (STDIN_FILENO),
		G_IO_IN, stdin_io, NULL);
	g_main_loop_run (loop);
	termo_destroy (tk);
}

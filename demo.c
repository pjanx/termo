// We want optarg
#define _XOPEN_SOURCE 600

#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <locale.h>

#include "termo.h"

int
main(int argc, char *argv[])
{
	TERMO_CHECK_VERSION;
	setlocale (LC_CTYPE, "");

	int mouse = 0;
	int mouse_proto = 0;
	termo_format_t format = TERMO_FORMAT_VIM;

	char buffer[50];
	termo_t *tk;

	int opt;
	while ((opt = getopt (argc, argv, "m::p:")) != -1)
	{
		switch (opt)
		{
		case 'm':
			if (optarg)
				mouse = atoi (optarg);
			else
				mouse = 1000;
			break;

		case 'p':
			mouse_proto = atoi (optarg);
			break;

		default:
			fprintf (stderr, "Usage: %s [-m]\n", argv[0]);
			return 1;
		}
	}

	tk = termo_new (STDIN_FILENO, NULL,
		TERMO_FLAG_SPACESYMBOL | TERMO_FLAG_CTRLC);
	if (!tk)
	{
		fprintf (stderr, "Cannot allocate termo instance\n");
		exit (1);
	}

	if (termo_get_flags (tk) & TERMO_FLAG_RAW)
		printf ("Termkey in RAW mode\n");
	else
		printf ("Termkey in multibyte mode\n");

	termo_result_t ret;
	termo_key_t key;

	if (mouse)
	{
		printf ("\033[?%dhMouse mode active\n", mouse);
		if (mouse_proto)
			printf ("\033[?%dh", mouse_proto);
	}

	while ((ret = termo_waitkey (tk, &key)) != TERMO_RES_EOF)
	{
		if (ret == TERMO_RES_KEY)
		{
			termo_strfkey (tk, buffer, sizeof buffer, &key, format);
			if (key.type == TERMO_TYPE_MOUSE)
			{
				int line, col;
				termo_interpret_mouse (tk, &key, NULL, NULL, &line, &col);
				printf ("%s at line=%d, col=%d\n", buffer, line, col);
			}
			else if (key.type == TERMO_TYPE_POSITION)
			{
				int line, col;
				termo_interpret_position (tk, &key, &line, &col);
				printf ("Cursor position report at line=%d, col=%d\n",
					line, col);
			}
			else if (key.type == TERMO_TYPE_MODEREPORT)
			{
				int initial, mode, value;
				termo_interpret_modereport
					(tk, &key, &initial, &mode, &value);
				printf ("Mode report %s mode %d = %d\n",
					initial ? "DEC" : "ANSI", mode, value);
			}
			else if (key.type == TERMO_TYPE_UNKNOWN_CSI)
			{
				long args[16];
				size_t nargs = 16;
				unsigned long command;
				termo_interpret_csi (tk, &key, args, &nargs, &command);
				printf ("Unrecognised CSI %c %ld;%ld %c%c\n",
					(char) (command >> 8), args[0], args[1],
					(char) (command >> 16), (char) command);
			}
			else
				printf ("Key %s\n", buffer);

			if (key.type == TERMO_TYPE_KEY
			 && key.modifiers & TERMO_KEYMOD_CTRL
			 && (key.code.codepoint == 'C' || key.code.codepoint == 'c'))
				break;

			if (key.type == TERMO_TYPE_KEY
			 && key.modifiers == 0
			 && key.code.codepoint == '?')
			{
				// printf("\033[?6n"); // DECDSR 6 == request cursor position
				printf ("\033[?1$p"); // DECRQM == request mode, DEC origin mode
				fflush (stdout);
			}
		}
		else if (ret == TERMO_RES_ERROR)
		{
			if (errno != EINTR)
			{
				perror ("termo_waitkey");
				break;
			}
			printf ("Interrupted by signal\n");
		}
	}

	if (mouse)
		printf ("\033[?%dlMouse mode deactivated\n", mouse);

	termo_destroy (tk);
}

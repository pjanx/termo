// <poll.h> might need this for sigset_t
#define _XOPEN_SOURCE 600

#include <stdio.h>
#include <unistd.h>
#include <locale.h>
#include <stdarg.h>
#include <stdbool.h>
#include <string.h>

#include <poll.h>
#include <signal.h>

#include <curses.h>
#include "termo.h"

typedef struct app_context app_context_t;
struct app_context
{
	termo_t *tk;

	// Current attributes for the left mouse button
	int current_attrs_left;
	// Current attributes for the right mouse button
	int current_attrs_right;
};

static int g_winch_pipe[2];

static void
display (const char *format, ...)
{
	va_list ap;

	mvwhline (stdscr, 0, 0, A_REVERSE, COLS);
	attron (A_REVERSE);

	va_start (ap, format);
	vw_printw (stdscr, format, ap);
	va_end (ap);

	attroff (A_REVERSE);
	refresh ();
}

static void
init_palette (app_context_t *app)
{
	start_color ();

	// Also does init_pair (0, -1, -1);
	use_default_colors ();
	// Duplicate it for convenience.
	init_pair (9, -1, -1);

	// Add the basic 8 colors to the default pair.  Once normally, once
	// inverted to workaround VTE's inability to set a bright background.
	for (int i = 0; i < 8; i++)
	{
		init_pair (1  + i, COLOR_WHITE, COLOR_BLACK + i);
		init_pair (10 + i, COLOR_BLACK + i, COLOR_WHITE);
	}

	// This usually creates a solid black or white.
	app->current_attrs_left =
	app->current_attrs_right = COLOR_PAIR (9) | A_REVERSE | A_BOLD;
}

static void
redraw (void)
{
	int i;

	mvwhline (stdscr, 1, 0, A_REVERSE, COLS);
	mvwhline (stdscr, 2, 0, A_REVERSE, COLS);

	for (i = 0; i < COLS; i++)
	{
		int pair = (float) i / COLS  * 9;
		mvaddch (1, i, ' ' | COLOR_PAIR (pair));
		mvaddch (2, i, ' ' | COLOR_PAIR (pair + 9) | A_REVERSE | A_BOLD);
	}

	display ("Choose a color from the palette and draw.  "
		"Press Escape or ^C to quit.");
	refresh ();
}

static bool
on_key (app_context_t *app, termo_key_t *key)
{
	if (key->type == TERMO_TYPE_KEYSYM
	 && key->code.sym == TERMO_SYM_ESCAPE)
		return false;

	if (key->type == TERMO_TYPE_KEY
	 && (key->modifiers & TERMO_KEYMOD_CTRL)
	 && (key->code.codepoint == 'C' || key->code.codepoint == 'c'))
		return false;

	if (key->type != TERMO_TYPE_MOUSE)
		return true;

	int line, col, button;
	termo_mouse_event_t event;

	termo_interpret_mouse (app->tk, key, &event, &button, &line, &col);
	if (event != TERMO_MOUSE_PRESS && event != TERMO_MOUSE_DRAG)
		return true;

	int *attrs;
	if (button == 1)
		attrs = &app->current_attrs_left;
	else if (button == 3)
		attrs = &app->current_attrs_right;
	else
		return true;

	chtype ch = mvwinch (stdscr, line, col);
	if (line >= 3)
	{
		// Paste the attributes where the user clicked.
		addch (' ' | *attrs);
		refresh ();
	}
	else if (line > 0)
		// Copy attributes from the pallete.
		*attrs = ch & (A_COLOR | A_ATTRIBUTES);

	return true;
}

static void
winch_handler (int signum)
{
	(void) signum;
	write (g_winch_pipe[1], "x", 1);
}

int
main (int argc, char *argv[])
{
	(void) argc;
	(void) argv;

	TERMO_CHECK_VERSION;
	setlocale (LC_CTYPE, "");

	struct sigaction act;
	act.sa_handler = winch_handler;
	act.sa_flags = SA_RESTART;
	sigemptyset (&act.sa_mask);

	// Set up a self-pipe so that we can actually poll on SIGWINCH
	if (sigaction (SIGWINCH, &act, NULL) || pipe (g_winch_pipe))
	{
		fprintf (stderr, "Cannot set up signal handler\n");
		exit (EXIT_FAILURE);
	}

	termo_t *tk = termo_new (STDIN_FILENO, NULL, 0);
	if (!tk)
	{
		fprintf (stderr, "Cannot allocate termo instance\n");
		exit (EXIT_FAILURE);
	}

	termo_set_mouse_tracking_mode (tk, TERMO_MOUSE_TRACKING_DRAG);

	// Set up curses for our drawing needs
	if (!initscr () || nonl () == ERR || curs_set (0) == ERR)
	{
		fprintf (stderr, "Cannot initialize curses\n");
		exit (EXIT_FAILURE);
	}

	app_context_t app;
	memset (&app, 0, sizeof app);
	app.tk = tk;

	init_palette (&app);
	redraw ();

	termo_result_t ret;
	termo_key_t key;

	// We listen for mouse/key input and terminal resize events
	struct pollfd fds[2] =
	{
		{ .fd = STDIN_FILENO,    .events = POLLIN },
		{ .fd = g_winch_pipe[0], .events = POLLIN },
	};

	// Run a simple event loop with poll()
	int nextwait = -1;
	bool running = true;
	while (running)
	{
		if (!poll (fds, 2, nextwait))
			if (termo_getkey_force (tk, &key) == TERMO_RES_KEY)
				running &= on_key (&app, &key);

		if (fds[1].revents & (POLLIN | POLLHUP | POLLERR))
		{
			char x;
			read (fds[1].fd, &x, 1);

			// The "official" simple and flicker-prone method of resizing
			// the internal buffers of curses
			endwin ();
			refresh ();

			redraw ();
		}
		if (fds[0].revents & (POLLIN | POLLHUP | POLLERR))
			termo_advisereadable (tk);

		while ((ret = termo_getkey (tk, &key)) == TERMO_RES_KEY)
			running &= on_key (&app, &key);

		nextwait = -1;
		if (ret == TERMO_RES_AGAIN)
			nextwait = termo_get_waittime (tk);
	}

	endwin ();
	termo_destroy (tk);
}


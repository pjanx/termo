#include "../termo.h"
#include "taplib.h"

int
main (int argc, char *argv[])
{
	termo_t *tk;
	termo_key_t key;
	termo_mouse_event_t ev;
	int button, line, col;
	char buffer[32];
	size_t len;

	plan_tests (60);

	tk = termo_new_abstract ("vt100", NULL, 0);

	termo_push_bytes (tk, "\e[M !!", 6);

	key.type = -1;
	is_int (termo_getkey (tk, &key), TERMO_RES_KEY,
		"getkey yields RES_KEY for mouse press");

	is_int (key.type, TERMO_TYPE_MOUSE, "key.type for mouse press");

	ev = -1; button = -1; line = -1; col = -1;
	is_int (termo_interpret_mouse (tk, &key, &ev, &button, &line, &col),
		TERMO_RES_KEY, "interpret_mouse yields RES_KEY");

	is_int (ev, TERMO_MOUSE_PRESS, "mouse event for press");
	is_int (button, 1, "mouse button for press");
	is_int (line, 1, "mouse line for press");
	is_int (col, 1, "mouse column for press");
	is_int (key.modifiers, 0, "modifiers for press");

	len = termo_strfkey (tk, buffer, sizeof buffer, &key, 0);
	is_int (len, 13, "string length for press");
	is_str (buffer, "MousePress(1)", "string buffer for press");

	len = termo_strfkey (tk, buffer, sizeof buffer,
		&key, TERMO_FORMAT_MOUSE_POS);
	is_int (len, 21, "string length for press");
	is_str (buffer, "MousePress(1) @ (1,1)", "string buffer for press");

	termo_push_bytes (tk, "\e[M@\"!", 6);

	key.type = -1;
	ev = -1; button = -1; line = -1; col = -1;
	termo_getkey (tk, &key);
	is_int (termo_interpret_mouse (tk, &key, &ev, &button, &line, &col),
		TERMO_RES_KEY, "interpret_mouse yields RES_KEY");

	is_int (ev, TERMO_MOUSE_DRAG, "mouse event for drag");
	is_int (button, 1, "mouse button for drag");
	is_int (line, 1, "mouse line for drag");
	is_int (col, 2, "mouse column for drag");
	is_int (key.modifiers, 0, "modifiers for press");

	termo_push_bytes (tk, "\e[M##!", 6);

	key.type = -1;
	ev = -1; button = -1; line = -1; col = -1;
	termo_getkey (tk, &key);
	is_int (termo_interpret_mouse (tk, &key, &ev, &button, &line, &col),
		TERMO_RES_KEY, "interpret_mouse yields RES_KEY");

	is_int (ev, TERMO_MOUSE_RELEASE, "mouse event for release");
	is_int (line, 1, "mouse line for release");
	is_int (col, 3, "mouse column for release");
	is_int (key.modifiers, 0, "modifiers for press");

	termo_push_bytes (tk, "\e[M0++", 6);

	key.type = -1;
	ev = -1; button = -1; line = -1; col = -1;
	termo_getkey (tk, &key);
	is_int (termo_interpret_mouse (tk, &key, &ev, &button, &line, &col),
		TERMO_RES_KEY, "interpret_mouse yields RES_KEY");

	is_int (ev, TERMO_MOUSE_PRESS, "mouse event for Ctrl-press");
	is_int (button, 1, "mouse button for Ctrl-press");
	is_int (line, 11, "mouse line for Ctrl-press");
	is_int (col, 11, "mouse column for Ctrl-press");
	is_int (key.modifiers, TERMO_KEYMOD_CTRL, "modifiers for Ctrl-press");

	len = termo_strfkey (tk, buffer, sizeof buffer, &key, 0);
	is_int (len, 15, "string length for Ctrl-press");
	is_str (buffer, "C-MousePress(1)", "string buffer for Ctrl-press");

	// rxvt protocol
	termo_push_bytes (tk, "\e[0;20;20M", 10);

	key.type = -1;
	is_int (termo_getkey (tk, &key), TERMO_RES_KEY,
		"getkey yields RES_KEY for mouse press rxvt protocol");

	is_int (key.type, TERMO_TYPE_MOUSE,
		"key.type for mouse press rxvt protocol");

	is_int (termo_interpret_mouse (tk, &key, &ev, &button, &line, &col),
		TERMO_RES_KEY, "interpret_mouse yields RES_KEY");

	is_int (ev, TERMO_MOUSE_PRESS, "mouse event for press rxvt protocol");
	is_int (button, 1, "mouse button for press rxvt protocol");
	is_int (line, 20, "mouse line for press rxvt protocol");
	is_int (col, 20, "mouse column for press rxvt protocol");
	is_int (key.modifiers, 0, "modifiers for press rxvt protocol");

	termo_push_bytes (tk, "\e[3;20;20M", 10);

	is_int (termo_getkey (tk, &key), TERMO_RES_KEY,
		"getkey yields RES_KEY for mouse release rxvt protocol");

	is_int (key.type, TERMO_TYPE_MOUSE,
		"key.type for mouse release rxvt protocol");

	ev = -1; button = -1; line = -1; col = -1;
	is_int (termo_interpret_mouse (tk, &key, &ev, &button, &line, &col),
		TERMO_RES_KEY, "interpret_mouse yields RES_KEY");

	is_int (ev, TERMO_MOUSE_RELEASE, "mouse event for release rxvt protocol");
	is_int (line, 20, "mouse line for release rxvt protocol");
	is_int (col, 20, "mouse column for release rxvt protocol");
	is_int (key.modifiers, 0, "modifiers for release rxvt protocol");

	// SGR protocol
	termo_push_bytes (tk, "\e[<0;30;30M", 11);

	key.type = -1;
	is_int (termo_getkey (tk, &key), TERMO_RES_KEY,
		"getkey yields RES_KEY for mouse press SGR encoding");

	is_int (key.type, TERMO_TYPE_MOUSE,
		"key.type for mouse press SGR encoding");

	ev = -1; button = -1; line = -1; col = -1;
	is_int (termo_interpret_mouse (tk, &key, &ev, &button, &line, &col),
		TERMO_RES_KEY, "interpret_mouse yields RES_KEY");

	is_int (ev, TERMO_MOUSE_PRESS, "mouse event for press SGR");
	is_int (button, 1, "mouse button for press SGR");
	is_int (line, 30, "mouse line for press SGR");
	is_int (col, 30, "mouse column for press SGR");
	is_int (key.modifiers, 0, "modifiers for press SGR");

	termo_push_bytes (tk, "\e[<0;30;30m", 11);

	key.type = -1;
	is_int (termo_getkey (tk, &key), TERMO_RES_KEY,
		"getkey yields RES_KEY for mouse release SGR encoding");

	is_int (key.type, TERMO_TYPE_MOUSE,
		"key.type for mouse release SGR encoding");

	ev = -1; button = -1; line = -1; col = -1;
	is_int (termo_interpret_mouse (tk, &key, &ev, &button, &line, &col),
		TERMO_RES_KEY, "interpret_mouse yields RES_KEY");

	is_int (ev, TERMO_MOUSE_RELEASE, "mouse event for release SGR");

	termo_push_bytes (tk, "\e[<0;500;300M", 13);

	key.type = -1;
	ev = -1; button = -1; line = -1; col = -1;
	termo_getkey (tk, &key);
	termo_interpret_mouse (tk, &key, &ev, &button, &line, &col);

	is_int (line, 300, "mouse line for press SGR wide");
	is_int (col, 500, "mouse column for press SGR wide");

	termo_destroy (tk);

	return exit_status ();
}

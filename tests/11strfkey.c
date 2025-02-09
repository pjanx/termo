#include "../termo.h"
#include "taplib.h"

int
main (int argc, char *argv[])
{
	(void) argc;
	(void) argv;

	termo_t *tk;
	termo_key_t key;
	char buffer[16];
	size_t len;

	plan_tests (44);

	tk = termo_new_abstract ("vt100", NULL, 0);

	key.type = TERMO_TYPE_KEY;
	key.code.codepoint = 'A';
	key.modifiers = 0;
	key.multibyte[0] = 0;

	len = termo_strfkey_utf8 (tk, buffer, sizeof buffer, &key, 0);
	is_int (len, 1, "length for unicode/A/0");
	is_str (buffer, "A", "buffer for unicode/A/0");

	len = termo_strfkey_utf8 (tk, buffer, sizeof buffer, &key,
		TERMO_FORMAT_WRAPBRACKET);
	is_int (len, 1, "length for unicode/A/0 wrapbracket");
	is_str (buffer, "A", "buffer for unicode/A/0 wrapbracket");

	key.type = TERMO_TYPE_KEY;
	key.code.codepoint = 'b';
	key.modifiers = TERMO_KEYMOD_CTRL;
	key.multibyte[0] = 0;

	len = termo_strfkey_utf8 (tk, buffer, sizeof buffer, &key, 0);
	is_int (len, 3, "length for unicode/b/CTRL");
	is_str (buffer, "C-b", "buffer for unicode/b/CTRL");

	len = termo_strfkey_utf8 (tk, buffer, sizeof buffer, &key,
		TERMO_FORMAT_LONGMOD);
	is_int (len, 6, "length for unicode/b/CTRL longmod");
	is_str (buffer, "Ctrl-b", "buffer for unicode/b/CTRL longmod");

	len = termo_strfkey_utf8 (tk, buffer, sizeof buffer, &key,
		TERMO_FORMAT_LONGMOD | TERMO_FORMAT_SPACEMOD);
	is_int (len, 6, "length for unicode/b/CTRL longmod|spacemod");
	is_str (buffer, "Ctrl b", "buffer for unicode/b/CTRL longmod|spacemod");

	len = termo_strfkey_utf8 (tk, buffer, sizeof buffer, &key,
		TERMO_FORMAT_LONGMOD | TERMO_FORMAT_LOWERMOD);
	is_int (len, 6, "length for unicode/b/CTRL longmod|lowermod");
	is_str (buffer, "ctrl-b", "buffer for unicode/b/CTRL longmod|lowermod");

	len = termo_strfkey_utf8 (tk, buffer, sizeof buffer, &key,
		TERMO_FORMAT_LONGMOD | TERMO_FORMAT_SPACEMOD
		| TERMO_FORMAT_LOWERMOD);
	is_int (len, 6, "length for unicode/b/CTRL longmod|spacemod|lowermode");
	is_str (buffer, "ctrl b",
		"buffer for unicode/b/CTRL longmod|spacemod|lowermode");

	len = termo_strfkey_utf8 (tk, buffer, sizeof buffer, &key,
		TERMO_FORMAT_CARETCTRL);
	is_int (len, 2, "length for unicode/b/CTRL caretctrl");
	is_str (buffer, "^B", "buffer for unicode/b/CTRL caretctrl");

	len = termo_strfkey_utf8 (tk, buffer, sizeof buffer, &key,
		TERMO_FORMAT_WRAPBRACKET);
	is_int (len, 5, "length for unicode/b/CTRL wrapbracket");
	is_str (buffer, "<C-b>", "buffer for unicode/b/CTRL wrapbracket");

	key.type = TERMO_TYPE_KEY;
	key.code.codepoint = 'c';
	key.modifiers = TERMO_KEYMOD_ALT;
	key.multibyte[0] = 0;

	len = termo_strfkey_utf8 (tk, buffer, sizeof buffer, &key, 0);
	is_int (len, 3, "length for unicode/c/ALT");
	is_str (buffer, "A-c", "buffer for unicode/c/ALT");

	len = termo_strfkey_utf8 (tk, buffer, sizeof buffer, &key,
		TERMO_FORMAT_LONGMOD);
	is_int (len, 5, "length for unicode/c/ALT longmod");
	is_str (buffer, "Alt-c", "buffer for unicode/c/ALT longmod");

	len = termo_strfkey_utf8 (tk, buffer, sizeof buffer, &key,
		TERMO_FORMAT_ALTISMETA);
	is_int (len, 3, "length for unicode/c/ALT altismeta");
	is_str (buffer, "M-c", "buffer for unicode/c/ALT altismeta");

	len = termo_strfkey_utf8 (tk, buffer, sizeof buffer, &key,
		TERMO_FORMAT_LONGMOD|TERMO_FORMAT_ALTISMETA);
	is_int (len, 6, "length for unicode/c/ALT longmod|altismeta");
	is_str (buffer, "Meta-c", "buffer for unicode/c/ALT longmod|altismeta");

	key.type = TERMO_TYPE_KEYSYM;
	key.code.sym = TERMO_SYM_UP;
	key.modifiers = 0;

	len = termo_strfkey_utf8 (tk, buffer, sizeof buffer, &key, 0);
	is_int (len, 2, "length for sym/Up/0");
	is_str (buffer, "Up", "buffer for sym/Up/0");

	len = termo_strfkey_utf8 (tk, buffer, sizeof buffer, &key,
		TERMO_FORMAT_WRAPBRACKET);
	is_int (len, 4, "length for sym/Up/0 wrapbracket");
	is_str (buffer, "<Up>", "buffer for sym/Up/0 wrapbracket");

	key.type = TERMO_TYPE_KEYSYM;
	key.code.sym = TERMO_SYM_PAGEUP;
	key.modifiers = 0;

	len = termo_strfkey_utf8 (tk, buffer, sizeof buffer, &key, 0);
	is_int (len, 6, "length for sym/PageUp/0");
	is_str (buffer, "PageUp", "buffer for sym/PageUp/0");

	len = termo_strfkey_utf8 (tk, buffer, sizeof buffer, &key,
		TERMO_FORMAT_LOWERSPACE);
	is_int (len, 7, "length for sym/PageUp/0 lowerspace");
	is_str (buffer, "page up", "buffer for sym/PageUp/0 lowerspace");

	// If size of buffer is too small,
	// strfkey_utf8 should return something consistent
	len = termo_strfkey_utf8 (tk, buffer, 4, &key, 0);
	is_int (len, 6, "length for sym/PageUp/0");
	is_str (buffer, "Pag", "buffer of len 4 for sym/PageUp/0");

	len = termo_strfkey_utf8 (tk, buffer, 4, &key, TERMO_FORMAT_LOWERSPACE);
	is_int (len, 7, "length for sym/PageUp/0 lowerspace");
	is_str (buffer, "pag", "buffer of len 4 for sym/PageUp/0 lowerspace");

	key.type = TERMO_TYPE_FUNCTION;
	key.code.number = 5;
	key.modifiers = 0;

	len = termo_strfkey_utf8 (tk, buffer, sizeof buffer, &key, 0);
	is_int (len, 2, "length for func/5/0");
	is_str (buffer, "F5", "buffer for func/5/0");

	len = termo_strfkey_utf8 (tk, buffer, sizeof buffer, &key,
		TERMO_FORMAT_WRAPBRACKET);
	is_int (len, 4, "length for func/5/0 wrapbracket");
	is_str (buffer, "<F5>", "buffer for func/5/0 wrapbracket");

	len = termo_strfkey_utf8 (tk, buffer, sizeof buffer, &key,
		TERMO_FORMAT_LOWERSPACE);
	is_int (len, 2, "length for func/5/0 lowerspace");
	is_str (buffer, "f5", "buffer for func/5/0 lowerspace");

	termo_destroy (tk);
	return exit_status ();
}

#include "../termo.h"
#include "taplib.h"

int
main (int argc, char *argv[])
{
	termo_t *tk;
	termo_key_t key;
	const char *endp;

#define CLEAR_KEY do { key.type = -1; key.code.codepoint = -1; \
	key.modifiers = -1; key.multibyte[0] = 0; } while (0)

	plan_tests (62);

	tk = termo_new_abstract ("vt100", NULL, 0);

	CLEAR_KEY;
	endp = termo_strpkey_utf8 (tk, "A", &key, 0);
	is_int (key.type, TERMO_TYPE_KEY, "key.type for unicode/A/0");
	is_int (key.code.codepoint, 'A', "key.code.codepoint for unicode/A/0");
	is_int (key.modifiers, 0, "key.modifiers for unicode/A/0");
	is_str (key.multibyte, "A", "key.multibyte for unicode/A/0");
	is_str (endp, "", "consumed entire input for unicode/A/0");

	CLEAR_KEY;
	endp = termo_strpkey_utf8 (tk, "A and more", &key, 0);
	is_int (key.type, TERMO_TYPE_KEY,
		"key.type for unicode/A/0 trailing");
	is_int (key.code.codepoint, 'A',
		"key.code.codepoint for unicode/A/0 trailing");
	is_int (key.modifiers, 0, "key.modifiers for unicode/A/0 trailing");
	is_str (key.multibyte, "A", "key.multibyte for unicode/A/0 trailing");
	is_str (endp, " and more",
		"points at string tail for unicode/A/0 trailing");

	CLEAR_KEY;
	endp = termo_strpkey_utf8 (tk, "C-b", &key, 0);
	is_int (key.type, TERMO_TYPE_KEY, "key.type for unicode/b/CTRL");
	is_int (key.code.codepoint, 'b', "key.code.codepoint for unicode/b/CTRL");
	is_int (key.modifiers, TERMO_KEYMOD_CTRL,
		"key.modifiers for unicode/b/CTRL");
	is_str (key.multibyte, "b", "key.multibyte for unicode/b/CTRL");
	is_str (endp, "", "consumed entire input for unicode/b/CTRL");

	CLEAR_KEY;
	endp = termo_strpkey_utf8 (tk, "Ctrl-b", &key, TERMO_FORMAT_LONGMOD);
	is_int (key.type, TERMO_TYPE_KEY,
		"key.type for unicode/b/CTRL longmod");
	is_int (key.code.codepoint, 'b',
		"key.code.codepoint for unicode/b/CTRL longmod");
	is_int (key.modifiers, TERMO_KEYMOD_CTRL,
		"key.modifiers for unicode/b/CTRL longmod");
	is_str (key.multibyte, "b", "key.multibyte for unicode/b/CTRL longmod");
	is_str (endp, "", "consumed entire input for unicode/b/CTRL longmod");

	CLEAR_KEY;
	endp = termo_strpkey_utf8 (tk, "^B", &key, TERMO_FORMAT_CARETCTRL);
	is_int (key.type, TERMO_TYPE_KEY,
		"key.type for unicode/b/CTRL caretctrl");
	is_int (key.code.codepoint, 'b',
		"key.code.codepoint for unicode/b/CTRL caretctrl");
	is_int (key.modifiers, TERMO_KEYMOD_CTRL,
		"key.modifiers for unicode/b/CTRL caretctrl");
	is_str (key.multibyte, "b", "key.multibyte for unicode/b/CTRL caretctrl");
	is_str (endp, "", "consumed entire input for unicode/b/CTRL caretctrl");

	CLEAR_KEY;
	endp = termo_strpkey_utf8 (tk, "A-c", &key, 0);
	is_int (key.type, TERMO_TYPE_KEY, "key.type for unicode/c/ALT");
	is_int (key.code.codepoint, 'c', "key.code.codepoint for unicode/c/ALT");
	is_int (key.modifiers, TERMO_KEYMOD_ALT,
		"key.modifiers for unicode/c/ALT");
	is_str (key.multibyte, "c", "key.multibyte for unicode/c/ALT");
	is_str (endp, "", "consumed entire input for unicode/c/ALT");

	CLEAR_KEY;
	endp = termo_strpkey_utf8 (tk, "Alt-c", &key, TERMO_FORMAT_LONGMOD);
	is_int (key.type, TERMO_TYPE_KEY,
		"key.type for unicode/c/ALT longmod");
	is_int (key.code.codepoint, 'c',
		"key.code.codepoint for unicode/c/ALT longmod");
	is_int (key.modifiers, TERMO_KEYMOD_ALT,
		"key.modifiers for unicode/c/ALT longmod");
	is_str (key.multibyte, "c", "key.multibyte for unicode/c/ALT longmod");
	is_str (endp, "", "consumed entire input for unicode/c/ALT longmod");

	CLEAR_KEY;
	endp = termo_strpkey_utf8 (tk, "M-c", &key, TERMO_FORMAT_ALTISMETA);
	is_int (key.type, TERMO_TYPE_KEY,
		"key.type for unicode/c/ALT altismeta");
	is_int (key.code.codepoint, 'c',
		"key.code.codepoint for unicode/c/ALT altismeta");
	is_int (key.modifiers, TERMO_KEYMOD_ALT,
		"key.modifiers for unicode/c/ALT altismeta");
	is_str (key.multibyte, "c", "key.multibyte for unicode/c/ALT altismeta");
	is_str (endp, "", "consumed entire input for unicode/c/ALT altismeta");

	CLEAR_KEY;
	endp = termo_strpkey_utf8 (tk, "Meta-c", &key,
		TERMO_FORMAT_ALTISMETA | TERMO_FORMAT_LONGMOD);
	is_int (key.type, TERMO_TYPE_KEY,
		"key.type for unicode/c/ALT altismeta+longmod");
	is_int (key.code.codepoint, 'c',
		"key.code.codepoint for unicode/c/ALT altismeta+longmod");
	is_int (key.modifiers, TERMO_KEYMOD_ALT,
		"key.modifiers for unicode/c/ALT altismeta+longmod");
	is_str (key.multibyte, "c", "key.multibyte for unicode/c/ALT altismeta+longmod");
	is_str (endp, "",
		"consumed entire input for unicode/c/ALT altismeta+longmod");

	CLEAR_KEY;
	endp = termo_strpkey_utf8 (tk, "meta c", &key,
		TERMO_FORMAT_ALTISMETA | TERMO_FORMAT_LONGMOD
		| TERMO_FORMAT_SPACEMOD | TERMO_FORMAT_LOWERMOD);
	is_int (key.type, TERMO_TYPE_KEY,
		"key.type for unicode/c/ALT altismeta+long/space+lowermod");
	is_int (key.code.codepoint, 'c',
		"key.code.codepoint for unicode/c/ALT altismeta+long/space+lowermod");
	is_int (key.modifiers, TERMO_KEYMOD_ALT,
		"key.modifiers for unicode/c/ALT altismeta+long/space+lowermod");
	is_str (key.multibyte, "c",
		"key.multibyte for unicode/c/ALT altismeta+long/space_lowermod");
	is_str (endp, "",
		"consumed entire input for unicode/c/ALT altismeta+long/space+lowermod");

	CLEAR_KEY;
	endp = termo_strpkey_utf8 (tk, "ctrl alt page up", &key,
		TERMO_FORMAT_LONGMOD | TERMO_FORMAT_SPACEMOD
		| TERMO_FORMAT_LOWERMOD | TERMO_FORMAT_LOWERSPACE);
	is_int (key.type, TERMO_TYPE_KEYSYM,
		"key.type for sym/PageUp/CTRL+ALT long/space/lowermod+lowerspace");
	is_int (key.code.sym, TERMO_SYM_PAGEUP,
		"key.code.codepoint for sym/PageUp/CTRL+ALT long/space/lowermod+lowerspace");
	is_int (key.modifiers, TERMO_KEYMOD_ALT | TERMO_KEYMOD_CTRL,
		"key.modifiers for sym/PageUp/CTRL+ALT long/space/lowermod+lowerspace");
	is_str (endp, "",
		"consumed entire input for sym/PageUp/CTRL+ALT"
		" long/space/lowermod+lowerspace");

	CLEAR_KEY;
	endp = termo_strpkey_utf8 (tk, "Up", &key, 0);
	is_int (key.type, TERMO_TYPE_KEYSYM, "key.type for sym/Up/0");
	is_int (key.code.sym, TERMO_SYM_UP, "key.code.codepoint for sym/Up/0");
	is_int (key.modifiers, 0, "key.modifiers for sym/Up/0");
	is_str (endp, "", "consumed entire input for sym/Up/0");

	CLEAR_KEY;
	endp = termo_strpkey_utf8 (tk, "F5", &key, 0);
	is_int (key.type, TERMO_TYPE_FUNCTION, "key.type for func/5/0");
	is_int (key.code.number, 5, "key.code.number for func/5/0");
	is_int (key.modifiers, 0, "key.modifiers for func/5/0");
	is_str (endp, "", "consumed entire input for func/5/0");

	termo_destroy (tk);
	return exit_status ();
}

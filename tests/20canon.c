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

	plan_tests (26);

	tk = termo_new_abstract ("vt100", NULL, 0);

	CLEAR_KEY;
	endp = termo_strpkey_utf8 (tk, " ", &key, 0);
	is_int (key.type, TERMO_TYPE_KEY, "key.type for SP/unicode");
	is_int (key.code.codepoint, ' ', "key.code.codepoint for SP/unicode");
	is_int (key.modifiers, 0, "key.modifiers for SP/unicode");
	is_str (key.multibyte, " ", "key.multibyte for SP/unicode");
	is_str (endp, "", "consumed entire input for SP/unicode");

	CLEAR_KEY;
	endp = termo_strpkey_utf8 (tk, "Space", &key, 0);
	is_int (key.type, TERMO_TYPE_KEY, "key.type for Space/unicode");
	is_int (key.code.codepoint, ' ', "key.code.codepoint for Space/unicode");
	is_int (key.modifiers, 0, "key.modifiers for Space/unicode");
	is_str (key.multibyte, " ", "key.multibyte for Space/unicode");
	is_str (endp, "", "consumed entire input for Space/unicode");

	termo_set_canonflags (tk,
		termo_get_canonflags (tk) | TERMO_CANON_SPACESYMBOL);

	CLEAR_KEY;
	endp = termo_strpkey_utf8 (tk, " ", &key, 0);
	is_int (key.type, TERMO_TYPE_KEYSYM, "key.type for SP/symbol");
	is_int (key.code.sym, TERMO_SYM_SPACE,
		"key.code.codepoint for SP/symbol");
	is_int (key.modifiers, 0, "key.modifiers for SP/symbol");
	is_str (endp, "", "consumed entire input for SP/symbol");

	CLEAR_KEY;
	endp = termo_strpkey_utf8 (tk, "Space", &key, 0);
	is_int (key.type, TERMO_TYPE_KEYSYM, "key.type for Space/symbol");
	is_int (key.code.sym, TERMO_SYM_SPACE,
		"key.code.codepoint for Space/symbol");
	is_int (key.modifiers, 0, "key.modifiers for Space/symbol");
	is_str (endp, "", "consumed entire input for Space/symbol");

	CLEAR_KEY;
	endp = termo_strpkey_utf8 (tk, "DEL", &key, 0);
	is_int (key.type, TERMO_TYPE_KEYSYM, "key.type for Del/unconverted");
	is_int (key.code.sym, TERMO_SYM_DEL,
		"key.code.codepoint for Del/unconverted");
	is_int (key.modifiers, 0, "key.modifiers for Del/unconverted");
	is_str (endp, "", "consumed entire input for Del/unconverted");

	termo_set_canonflags (tk,
		termo_get_canonflags (tk) | TERMO_CANON_DELBS);

	CLEAR_KEY;
	endp = termo_strpkey_utf8 (tk, "DEL", &key, 0);
	is_int (key.type, TERMO_TYPE_KEYSYM, "key.type for Del/as-backspace");
	is_int (key.code.sym, TERMO_SYM_BACKSPACE,
		"key.code.codepoint for Del/as-backspace");
	is_int (key.modifiers, 0, "key.modifiers for Del/as-backspace");
	is_str (endp, "", "consumed entire input for Del/as-backspace");

	termo_destroy (tk);
	return exit_status ();
}

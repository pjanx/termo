#include <stdio.h>
#include "../termo.h"
#include "taplib.h"

int
main (int argc, char *argv[])
{
	(void) argc;
	(void) argv;

	termo_t *tk;
	termo_key_t key;

	plan_tests (8);

	tk = termo_new_abstract ("vt100", NULL, 0);

	termo_push_bytes (tk, " ", 1);

	is_int (termo_getkey (tk, &key), TERMO_RES_KEY,
		"getkey yields RES_KEY after space");

	is_int (key.type, TERMO_TYPE_KEY, "key.type after space");
	is_int (key.code.codepoint, ' ', "key.code.codepoint after space");
	is_int (key.modifiers, 0, "key.modifiers after space");

	termo_set_flags (tk, TERMO_FLAG_SPACESYMBOL);

	termo_push_bytes (tk, " ", 1);

	is_int (termo_getkey (tk, &key), TERMO_RES_KEY,
		"getkey yields RES_KEY after space");

	is_int (key.type, TERMO_TYPE_KEYSYM,
		"key.type after space with FLAG_SPACESYMBOL");
	is_int (key.code.sym, TERMO_SYM_SPACE,
		"key.code.sym after space with FLAG_SPACESYMBOL");
	is_int (key.modifiers, 0,
		"key.modifiers after space with FLAG_SPACESYMBOL");

	termo_destroy (tk);
	return exit_status ();
}

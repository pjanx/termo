#include "../termo.h"
#include "taplib.h"

int
main (int argc, char *argv[])
{
	termo_t *tk;
	termo_key_t key;

	plan_tests (6);

	tk = termo_new_abstract ("vt100", NULL, 0);

	termo_push_bytes (tk, "\e[I", 3);
	is_int (termo_getkey (tk, &key), TERMO_RES_KEY,
		"getkey yields RES_KEY for focus in");
	is_int (key.type, TERMO_TYPE_FOCUS, "key.type for focus in");
	is_int (key.code.focused, 1, "focused indicator for focus in");

	termo_push_bytes (tk, "\e[O", 3);
	is_int (termo_getkey (tk, &key), TERMO_RES_KEY,
		"getkey yields RES_KEY for focus out");
	is_int (key.type, TERMO_TYPE_FOCUS, "key.type for focus out");
	is_int (key.code.focused, 0, "focused indicator for focus out");

	termo_destroy (tk);
	return exit_status ();
}

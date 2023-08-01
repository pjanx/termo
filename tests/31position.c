#include "../termo.h"
#include "taplib.h"

int
main (int argc, char *argv[])
{
	(void) argc;
	(void) argv;

	termo_t *tk;
	termo_key_t key;
	int line, col;

	plan_tests (8);

	tk = termo_new_abstract ("vt100", NULL, 0);

	termo_push_bytes (tk, "\e[?15;7R", 8);

	is_int (termo_getkey (tk, &key), TERMO_RES_KEY,
		"getkey yields RES_KEY for position report");

	is_int (key.type, TERMO_TYPE_POSITION, "key.type for position report");

	is_int (termo_interpret_position (tk, &key, &line, &col), TERMO_RES_KEY,
		"interpret_position yields RES_KEY");

	is_int (line, 14, "line for position report");
	is_int (col, 6, "column for position report");

	// A plain CSI R is likely to be <F3> though.  This is tricky :/
	termo_push_bytes (tk, "\e[R", 3);

	is_int (termo_getkey (tk, &key), TERMO_RES_KEY,
		"getkey yields RES_KEY for <F3>");

	is_int (key.type, TERMO_TYPE_FUNCTION, "key.type for <F3>");
	is_int (key.code.number, 3, "key.code.number for <F3>");

	termo_destroy (tk);
	return exit_status ();
}

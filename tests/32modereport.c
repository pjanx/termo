#include "../termo.h"
#include "taplib.h"

int
main (int argc, char *argv[])
{
	termo_t *tk;
	termo_key_t key;
	int initial, mode, value;

	plan_tests (12);

	tk = termo_new_abstract ("vt100", NULL, 0);

	termo_push_bytes (tk, "\e[?1;2$y", 8);

	is_int (termo_getkey (tk, &key), TERMO_RES_KEY,
		"getkey yields RES_KEY for mode report");

	is_int (key.type, TERMO_TYPE_MODEREPORT, "key.type for mode report");

	is_int (termo_interpret_modereport (tk, &key, &initial, &mode, &value),
		TERMO_RES_KEY, "interpret_modereoprt yields RES_KEY");

	is_int (initial, '?', "initial indicator from mode report");
	is_int (mode, 1, "mode number from mode report");
	is_int (value, 2, "mode value from mode report");

	termo_push_bytes (tk, "\e[4;1$y", 7);

	is_int (termo_getkey (tk, &key), TERMO_RES_KEY,
		"getkey yields RES_KEY for mode report");

	is_int (key.type, TERMO_TYPE_MODEREPORT, "key.type for mode report");

	is_int (termo_interpret_modereport (tk, &key, &initial, &mode, &value),
		TERMO_RES_KEY, "interpret_modereoprt yields RES_KEY");

	is_int (initial, 0, "initial indicator from mode report");
	is_int (mode, 4, "mode number from mode report");
	is_int (value, 1, "mode value from mode report");

	termo_destroy (tk);
	return exit_status ();
}

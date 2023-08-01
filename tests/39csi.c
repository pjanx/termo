#include "../termo.h"
#include "taplib.h"

int
main (int argc, char *argv[])
{
	(void) argc;
	(void) argv;

	termo_t *tk;
	termo_key_t key;
	long args[16];
	size_t nargs = 16;
	unsigned long command;

	plan_tests (15);

	tk = termo_new_abstract ("vt100", NULL, 0);

	termo_push_bytes (tk, "\e[5;25v", 7);

	is_int (termo_getkey (tk, &key), TERMO_RES_KEY,
		"getkey yields RES_KEY for CSI v");

	is_int (key.type, TERMO_TYPE_UNKNOWN_CSI, "key.type for unknown CSI");

	is_int (termo_interpret_csi (tk, &key, args, &nargs, &command),
		TERMO_RES_KEY, "interpret_csi yields RES_KEY");

	is_int (nargs, 2, "nargs for unknown CSI");
	is_int (args[0], 5, "args[0] for unknown CSI");
	is_int (args[1], 25, "args[1] for unknown CSI");
	is_int (command, 'v', "command for unknown CSI");

	termo_push_bytes (tk, "\e[?w", 4);

	is_int (termo_getkey (tk, &key), TERMO_RES_KEY,
		"getkey yields RES_KEY for CSI ? w");
	is_int (key.type, TERMO_TYPE_UNKNOWN_CSI, "key.type for unknown CSI");
	is_int (termo_interpret_csi (tk, &key, args, &nargs, &command),
		TERMO_RES_KEY, "interpret_csi yields RES_KEY");
	is_int (command, ('?' << 8) | 'w', "command for unknown CSI");

	termo_push_bytes (tk, "\e[?$x", 5);

	is_int (termo_getkey (tk, &key), TERMO_RES_KEY,
		"getkey yields RES_KEY for CSI ? $x");
	is_int (key.type, TERMO_TYPE_UNKNOWN_CSI, "key.type for unknown CSI");
	is_int (termo_interpret_csi (tk, &key, args, &nargs, &command),
		TERMO_RES_KEY, "interpret_csi yields RES_KEY");
	is_int (command, ('$' << 16) | ('?' << 8) | 'x', "command for unknown CSI");

	termo_destroy (tk);
	return exit_status ();
}

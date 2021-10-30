#include <stdio.h>
#include "../termo.h"
#include "taplib.h"

int
main (int argc, char *argv[])
{
	termo_t *tk;
	termo_key_t key;

	plan_tests (9);

	tk = termo_new_abstract ("vt100", NULL, 0);

	is_int (termo_get_buffer_remaining (tk), 256,
		"buffer free initially 256");
	is_int (termo_get_buffer_size (tk), 256,
		"buffer size initially 256");

	is_int (termo_push_bytes (tk, "h", 1), 1, "push_bytes returns 1");

	is_int (termo_get_buffer_remaining (tk), 255,
		"buffer free 255 after push_bytes");
	is_int (termo_get_buffer_size (tk), 256,
		"buffer size 256 after push_bytes");

	ok (!!termo_set_buffer_size (tk, 512), "buffer set size OK");

	is_int (termo_get_buffer_remaining (tk), 511,
		"buffer free 511 after push_bytes");
	is_int (termo_get_buffer_size (tk), 512,
		"buffer size 512 after push_bytes");

	is_int (termo_getkey (tk, &key), TERMO_RES_KEY,
		"buffered key still useable after resize");

	termo_destroy (tk);
	return exit_status ();
}

#include <stdio.h>
#include "../termo.h"
#include "taplib.h"

int
main (int argc, char *argv[])
{
	(void) argc;
	(void) argv;

	termo_t *tk;

	plan_tests (6);

	tk = termo_new_abstract ("vt100", NULL, 0);
	ok (!!tk, "termo_new_abstract");
	is_int (termo_get_buffer_size (tk), 256, "termo_get_buffer_size");
	ok (termo_is_started (tk), "termo_is_started true after construction");

	termo_stop (tk);
	ok (!termo_is_started (tk),
		"termo_is_started false after termo_stop()");

	termo_start (tk);
	ok (termo_is_started (tk),
		"termo_is_started true after termo_start()");

	termo_destroy (tk);

	ok (1, "termo_free");
	return exit_status ();
}

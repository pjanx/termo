#include "../termo.h"
#include "taplib.h"

int
main (int argc, char *argv[])
{
	termo_t *tk;
	termo_sym_t sym;
	const char *end;

	plan_tests (10);

	tk = termo_new_abstract ("vt100", NULL, 0);

	sym = termo_keyname2sym (tk, "Space");
	is_int (sym, TERMO_SYM_SPACE, "keyname2sym Space");

	sym = termo_keyname2sym (tk, "SomeUnknownKey");
	is_int (sym, TERMO_SYM_UNKNOWN, "keyname2sym SomeUnknownKey");

	end = termo_lookup_keyname (tk, "Up", &sym);
	ok (!!end, "termo_get_keyname Up returns non-NULL");
	is_str (end, "", "termo_get_keyname Up return points at endofstring");
	is_int (sym, TERMO_SYM_UP, "termo_get_keyname Up yields Up symbol");

	end = termo_lookup_keyname (tk, "DownMore", &sym);
	ok (!!end, "termo_get_keyname DownMore returns non-NULL");
	is_str (end, "More", "termo_get_keyname DownMore return points at More");
	is_int (sym, TERMO_SYM_DOWN,
		"termo_get_keyname DownMore yields Down symbol");

	end = termo_lookup_keyname (tk, "SomeUnknownKey", &sym);
	ok (!end, "termo_get_keyname SomeUnknownKey returns NULL");

	is_str (termo_get_keyname (tk, TERMO_SYM_SPACE), "Space",
		"get_keyname SPACE");

	termo_destroy (tk);
	return exit_status ();
}

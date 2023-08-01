#include "../termo.h"
#include "taplib.h"

int
main (int argc, char *argv[])
{
	(void) argc;
	(void) argv;

	termo_t *tk;
	termo_key_t key1, key2;

	plan_tests (12);

	tk = termo_new_abstract ("vt100", NULL, 0);

	key1.type = TERMO_TYPE_KEY;
	key1.code.codepoint = 'A';
	key1.modifiers = 0;

	is_int (termo_keycmp (tk, &key1, &key1), 0, "cmpkey same structure");

	key2.type = TERMO_TYPE_KEY;
	key2.code.codepoint = 'A';
	key2.modifiers = 0;

	is_int (termo_keycmp (tk, &key1, &key2), 0, "cmpkey identical structure");

	key2.modifiers = TERMO_KEYMOD_CTRL;

	ok (termo_keycmp (tk, &key1, &key2) < 0,
		"cmpkey orders CTRL after nomod");
	ok (termo_keycmp (tk, &key2, &key1) > 0,
		"cmpkey orders nomod before CTRL");

	key2.code.codepoint = 'B';
	key2.modifiers = 0;

	ok (termo_keycmp (tk, &key1, &key2) < 0, "cmpkey orders 'B' after 'A'");
	ok (termo_keycmp (tk, &key2, &key1) > 0, "cmpkey orders 'A' before 'B'");

	key1.modifiers = TERMO_KEYMOD_CTRL;

	ok (termo_keycmp (tk, &key1, &key2) < 0,
		"cmpkey orders nomod 'B' after CTRL 'A'");
	ok (termo_keycmp (tk, &key2, &key1) > 0,
		"cmpkey orders CTRL 'A' before nomod 'B'");

	key2.type = TERMO_TYPE_KEYSYM;
	key2.code.sym = TERMO_SYM_UP;

	ok (termo_keycmp (tk, &key1, &key2) < 0,
		"cmpkey orders KEYSYM after KEY");
	ok (termo_keycmp (tk, &key2, &key1) > 0,
		"cmpkey orders KEY before KEYSYM");

	key1.type = TERMO_TYPE_KEYSYM;
	key1.code.sym = TERMO_SYM_SPACE;
	key1.modifiers = 0;
	key2.type = TERMO_TYPE_KEY;
	key2.code.codepoint = ' ';
	key2.modifiers = 0;

	is_int (termo_keycmp (tk, &key1, &key2), 0,
		"cmpkey considers KEYSYM/SPACE and KEY/SP identical");

	termo_set_canonflags (tk,
		termo_get_canonflags (tk) | TERMO_CANON_SPACESYMBOL);
	is_int (termo_keycmp (tk, &key1, &key2), 0,
		"cmpkey considers KEYSYM/SPACE and KEY/SP"
		" identical under SPACESYMBOL");

	termo_destroy (tk);
	return exit_status ();
}

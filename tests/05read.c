#define _XOPEN_SOURCE

#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include "../termo.h"
#include "taplib.h"

int
main (int argc, char *argv[])
{
	int fd[2];
	termo_t *tk;
	termo_key_t key;

	plan_tests (21);

	// We'll need a real filehandle we can write/read.  pipe() can make us one
	pipe (fd);

	// Sanitise this just in case
	putenv ("TERM=vt100");

	tk = termo_new (fd[0], NULL, TERMO_FLAG_NOTERMIOS);

	is_int (termo_get_buffer_remaining (tk), 256,
		"buffer free initially 256");

	is_int (termo_getkey (tk, &key), TERMO_RES_NONE,
		"getkey yields RES_NONE when empty");

	write (fd[1], "h", 1);

	is_int (termo_getkey (tk, &key), TERMO_RES_NONE,
		"getkey yields RES_NONE before advisereadable");

	is_int (termo_advisereadable (tk), TERMO_RES_AGAIN,
		"advisereadable yields RES_AGAIN after h");

	is_int (termo_get_buffer_remaining (tk), 255,
		"buffer free 255 after advisereadable");

	is_int (termo_getkey (tk, &key), TERMO_RES_KEY,
		"getkey yields RES_KEY after h");

	is_int (key.type, TERMO_TYPE_KEY, "key.type after h");
	is_int (key.code.codepoint, 'h', "key.code.codepoint after h");
	is_int (key.modifiers, 0, "key.modifiers after h");
	is_str (key.multibyte, "h", "key.multibyte after h");

	is_int (termo_get_buffer_remaining (tk), 256,
		"buffer free 256 after getkey");

	is_int (termo_getkey (tk, &key), TERMO_RES_NONE,
		"getkey yields RES_NONE a second time");

	write (fd[1], "\033O", 2);
	termo_advisereadable (tk);

	is_int (termo_get_buffer_remaining (tk), 254,
		"buffer free 254 after partial write");

	is_int (termo_getkey (tk, &key), TERMO_RES_AGAIN,
		"getkey yields RES_AGAIN after partial write");

	write (fd[1], "C", 1);
	termo_advisereadable (tk);

	is_int (termo_getkey (tk, &key), TERMO_RES_KEY,
		"getkey yields RES_KEY after Right completion");

	is_int (key.type, TERMO_TYPE_KEYSYM, "key.type after Right");
	is_int (key.code.sym, TERMO_SYM_RIGHT, "key.code.sym after Right");
	is_int (key.modifiers, 0, "key.modifiers after Right");

	is_int (termo_get_buffer_remaining (tk), 256,
		"buffer free 256 after completion");

	termo_stop (tk);

	is_int (termo_getkey (tk, &key), TERMO_RES_ERROR,
		"getkey yields RES_ERROR after termo_stop ()");
	is_int (errno, EINVAL, "getkey error is EINVAL");

	termo_destroy (tk);
	return exit_status ();
}

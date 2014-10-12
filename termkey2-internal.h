#ifndef TERMKEY2_INTERNAL_H
#define TERMKEY2_INTERNAL_H

#include "termkey2.h"

#include <stdint.h>
#include <termios.h>
#include <stdbool.h>
#include <iconv.h>

typedef struct termkey_driver termkey_driver_t;
struct termkey_driver
{
	const char *name;
	void *(*new_driver) (termkey_t *tk, const char *term);
	void (*free_driver) (void *info);
	int (*start_driver) (termkey_t *tk, void *info);
	int (*stop_driver) (termkey_t *tk, void *info);
	termkey_result_t (*peekkey) (termkey_t *tk,
		void *info, termkey_key_t *key, int force, size_t *nbytes);
};

typedef struct keyinfo keyinfo_t;
struct keyinfo
{
	termkey_type_t type;
	termkey_sym_t sym;
	int modifier_mask;
	int modifier_set;
};

typedef struct termkey_driver_node termkey_driver_node_t;
struct termkey_driver_node
{
	termkey_driver_t *driver;
	void *info;
	termkey_driver_node_t *next;
};

struct termkey
{
	int fd;
	int flags;
	int canonflags;

	unsigned char *buffer;
	size_t buffstart; // First offset in buffer
	size_t buffcount; // Number of entires valid in buffer
	size_t buffsize; // Total malloc'ed size

	// Position beyond buffstart at which peekkey() should next start.
	// Normally 0, but see also termkey_interpret_csi().
	size_t hightide;

	struct termios restore_termios;
	bool restore_termios_valid;

	int waittime; // In milliseconds

	bool is_closed; // We've received EOF
	bool is_started;

	int nkeynames;
	const char **keynames;

	keyinfo_t c0[32]; // There are 32 C0 codes
	iconv_t to_utf32_conv;
	iconv_t from_utf32_conv;
	termkey_driver_node_t *drivers;

	// Now some "protected" methods for the driver to call but which we don't
	// want exported as real symbols in the library
	struct
	{
		void (*emit_codepoint) (termkey_t *tk,
			uint32_t codepoint, termkey_key_t *key);
		termkey_result_t (*peekkey_simple) (termkey_t *tk,
			termkey_key_t *key, int force, size_t *nbytes);
		termkey_result_t (*peekkey_mouse) (termkey_t *tk,
			termkey_key_t *key, size_t *nbytes);
	}
	method;
};

static inline void
termkey_key_get_linecol (const termkey_key_t *key, int *line, int *col)
{
	if (col)
		*col = ((unsigned char) key->code.mouse[1]
			| ((unsigned char) key->code.mouse[3] & 0x0f) << 8) - 1;

	if (line)
		*line = ((unsigned char) key->code.mouse[2]
			| ((unsigned char) key->code.mouse[3] & 0x70) << 4) - 1;
}

static inline void
termkey_key_set_linecol (termkey_key_t *key, int line, int col)
{
	if (++line > 0xfff)
		line = 0xfff;

	if (++col > 0x7ff)
		col = 0x7ff;

	key->code.mouse[1] = (line & 0x0ff);
	key->code.mouse[2] = (col & 0x0ff);
	key->code.mouse[3] = (line & 0xf00) >> 8 | (col & 0x300) >> 4;
}

extern termkey_driver_t termkey_driver_csi;
extern termkey_driver_t termkey_driver_ti;

#endif  // ! TERMKEY2_INTERNAL_H


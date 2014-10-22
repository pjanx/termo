#ifndef TERMO_INTERNAL_H
#define TERMO_INTERNAL_H

#include "termo.h"

#include <stdint.h>
#include <termios.h>
#include <stdbool.h>
#include <iconv.h>

typedef struct termo_driver termo_driver_t;
struct termo_driver
{
	const char *name;
	void *(*new_driver) (termo_t *tk, const char *term);
	void (*free_driver) (void *info);
	int (*start_driver) (termo_t *tk, void *info);
	int (*stop_driver) (termo_t *tk, void *info);
	termo_result_t (*peekkey) (termo_t *tk,
		void *info, termo_key_t *key, int force, size_t *nbytes);
};

typedef struct keyinfo keyinfo_t;
struct keyinfo
{
	termo_type_t type;
	termo_sym_t sym;
	int modifier_mask;
	int modifier_set;
};

typedef struct termo_driver_node termo_driver_node_t;
struct termo_driver_node
{
	termo_driver_t *driver;
	void *info;
	termo_driver_node_t *next;
};

struct termo
{
	int fd;
	int flags;
	int canonflags;

	unsigned char *buffer;
	size_t buffstart; // First offset in buffer
	size_t buffcount; // Number of entires valid in buffer
	size_t buffsize; // Total malloc'ed size

	// Position beyond buffstart at which peekkey() should next start.
	// Normally 0, but see also termo_interpret_csi().
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
	termo_driver_node_t *drivers;

	// Now some "protected" methods for the driver to call but which we don't
	// want exported as real symbols in the library
	struct
	{
		void (*emit_codepoint) (termo_t *tk,
			uint32_t codepoint, termo_key_t *key);
		termo_result_t (*peekkey_simple) (termo_t *tk,
			termo_key_t *key, int force, size_t *nbytes);
		termo_result_t (*peekkey_mouse) (termo_t *tk,
			termo_key_t *key, size_t *nbytes);
	}
	method;

	int guessed_mouse_proto; // What we think should be the mouse protocol
	int mouse_proto; // The active mouse protocol
	termo_mouse_tracking_t mouse_tracking; // Mouse tracking mode

	// The mouse unfortunately directly depends on the terminfo driver to let
	// it handle changes in the mouse protocol.

	void *ti_data; // termo_ti_t pointer

	struct
	{
		bool (*set_mouse_proto) (void *, int, bool);
		bool (*set_mouse_tracking_mode) (void *, termo_mouse_tracking_t, bool);
	}
	ti_method;
};

static inline void
termo_key_get_linecol (const termo_key_t *key, int *line, int *col)
{
	if (col)
		*col = key->code.mouse.x;

	if (line)
		*line = key->code.mouse.y;
}

static inline void
termo_key_set_linecol (termo_key_t *key, int line, int col)
{
	if (line > UINT16_MAX)
		line = UINT16_MAX;

	if (col > UINT16_MAX)
		col = UINT16_MAX;

	key->code.mouse.x = col;
	key->code.mouse.y = line;
}

extern termo_driver_t termo_driver_csi;
extern termo_driver_t termo_driver_ti;

#endif  // ! TERMO_INTERNAL_H


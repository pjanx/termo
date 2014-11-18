#ifndef TERMO_H
#define TERMO_H

#include <stdint.h>
#include <stdlib.h>
#include <limits.h>

#include "termo-config.h"

#define TERMO_CHECK_VERSION \
	termo_check_version (TERMO_VERSION_MAJOR, TERMO_VERSION_MINOR)

typedef enum termo_sym termo_sym_t;
enum termo_sym
{
	TERMO_SYM_UNKNOWN = -1,
	TERMO_SYM_NONE = 0,

	// Special names in C0
	TERMO_SYM_BACKSPACE,
	TERMO_SYM_TAB,
	TERMO_SYM_ENTER,
	TERMO_SYM_ESCAPE,

	// Special names in G0
	TERMO_SYM_SPACE,
	TERMO_SYM_DEL,

	// Special keys
	TERMO_SYM_UP,
	TERMO_SYM_DOWN,
	TERMO_SYM_LEFT,
	TERMO_SYM_RIGHT,
	TERMO_SYM_BEGIN,
	TERMO_SYM_FIND,
	TERMO_SYM_INSERT,
	TERMO_SYM_DELETE,
	TERMO_SYM_SELECT,
	TERMO_SYM_PAGEUP,
	TERMO_SYM_PAGEDOWN,
	TERMO_SYM_HOME,
	TERMO_SYM_END,

	// Special keys from terminfo
	TERMO_SYM_CANCEL,
	TERMO_SYM_CLEAR,
	TERMO_SYM_CLOSE,
	TERMO_SYM_COMMAND,
	TERMO_SYM_COPY,
	TERMO_SYM_EXIT,
	TERMO_SYM_HELP,
	TERMO_SYM_MARK,
	TERMO_SYM_MESSAGE,
	TERMO_SYM_MOVE,
	TERMO_SYM_OPEN,
	TERMO_SYM_OPTIONS,
	TERMO_SYM_PRINT,
	TERMO_SYM_REDO,
	TERMO_SYM_REFERENCE,
	TERMO_SYM_REFRESH,
	TERMO_SYM_REPLACE,
	TERMO_SYM_RESTART,
	TERMO_SYM_RESUME,
	TERMO_SYM_SAVE,
	TERMO_SYM_SUSPEND,
	TERMO_SYM_UNDO,

	// Numeric keypad special keys
	TERMO_SYM_KP0,
	TERMO_SYM_KP1,
	TERMO_SYM_KP2,
	TERMO_SYM_KP3,
	TERMO_SYM_KP4,
	TERMO_SYM_KP5,
	TERMO_SYM_KP6,
	TERMO_SYM_KP7,
	TERMO_SYM_KP8,
	TERMO_SYM_KP9,
	TERMO_SYM_KPENTER,
	TERMO_SYM_KPPLUS,
	TERMO_SYM_KPMINUS,
	TERMO_SYM_KPMULT,
	TERMO_SYM_KPDIV,
	TERMO_SYM_KPCOMMA,
	TERMO_SYM_KPPERIOD,
	TERMO_SYM_KPEQUALS,

	TERMO_N_SYMS
};

typedef enum termo_type termo_type_t;
enum termo_type
{
	TERMO_TYPE_KEY,
	TERMO_TYPE_FUNCTION,
	TERMO_TYPE_KEYSYM,
	TERMO_TYPE_MOUSE,
	TERMO_TYPE_POSITION,
	TERMO_TYPE_MODEREPORT,
	// add other recognised types here

	TERMO_TYPE_UNKNOWN_CSI = -1
};

typedef enum termo_result termo_result_t;
enum termo_result
{
	TERMO_RES_NONE,
	TERMO_RES_KEY,
	TERMO_RES_EOF,
	TERMO_RES_AGAIN,
	TERMO_RES_ERROR
};

typedef enum termo_mouse_event termo_mouse_event_t;
enum termo_mouse_event
{
	TERMO_MOUSE_UNKNOWN,
	TERMO_MOUSE_PRESS,
	TERMO_MOUSE_DRAG,
	TERMO_MOUSE_RELEASE
};

// You will want to handle GPM (incompatible license) and FreeBSD's sysmouse
// yourself.  http://www.monkey.org/openbsd/archive/tech/0009/msg00018.html

enum
{
	TERMO_MOUSE_PROTO_NONE  = 0,
	TERMO_MOUSE_PROTO_VT200 = 1 << 0,
	TERMO_MOUSE_PROTO_UTF8  = 1 << 1 | TERMO_MOUSE_PROTO_VT200,
	TERMO_MOUSE_PROTO_SGR   = 1 << 2 | TERMO_MOUSE_PROTO_VT200,
	TERMO_MOUSE_PROTO_RXVT  = 1 << 3 | TERMO_MOUSE_PROTO_VT200
};

typedef enum termo_mouse_tracking termo_mouse_tracking_t;
enum termo_mouse_tracking
{
	TERMO_MOUSE_TRACKING_NORMAL,
	TERMO_MOUSE_TRACKING_DRAG,
	TERMO_MOUSE_TRACKING_ANY
};

enum
{
	TERMO_KEYMOD_SHIFT = 1 << 0,
	TERMO_KEYMOD_ALT   = 1 << 1,
	TERMO_KEYMOD_CTRL  = 1 << 2
};

typedef struct termo_key termo_key_t;
struct termo_key
{
	termo_type_t type;
	union
	{
		uint32_t    codepoint; // TERMO_TYPE_KEY
		int         number;    // TERMO_TYPE_FUNCTION
		termo_sym_t sym;       // TERMO_TYPE_KEYSYM

		// TERMO_TYPE_MODEREPORT
		// opaque, see termo_interpret_modereport()
		struct { char initial; int mode, value; } mode;

		// TERMO_TYPE_MOUSE
		// opaque, see termo_interpret_mouse()
		struct { int16_t x, y, info; } mouse;
	} code;

	int modifiers;

	// The raw multibyte sequence for the key
	char multibyte[MB_LEN_MAX + 1];
};

typedef struct termo termo_t;

enum
{
	// Do not interpret C0//DEL codes if possible
	TERMO_FLAG_NOINTERPRET = 1 << 0,
	// Convert KP codes to regular keypresses
	TERMO_FLAG_CONVERTKP   = 1 << 1,
	// Don't try to decode the input characters
	TERMO_FLAG_RAW         = 1 << 2,
	// Do not make initial termios calls on construction
	TERMO_FLAG_NOTERMIOS   = 1 << 4,
	// Sets TERMO_CANON_SPACESYMBOL
	TERMO_FLAG_SPACESYMBOL = 1 << 5,
	// Allow Ctrl-C to be read as normal, disabling SIGINT
	TERMO_FLAG_CTRLC       = 1 << 6,
	// Return ERROR on signal (EINTR) rather than retry
	TERMO_FLAG_EINTR       = 1 << 7
};

enum
{
	TERMO_CANON_SPACESYMBOL = 1 << 0, // Space is symbolic rather than Unicode
	TERMO_CANON_DELBS       = 1 << 1  // Del is converted to Backspace
};

void termo_check_version (int major, int minor);

termo_t *termo_new (int fd, const char *encoding, int flags);
termo_t *termo_new_abstract (const char *term,
	const char *encoding, int flags);
void termo_free (termo_t *tk);
void termo_destroy (termo_t *tk);

int termo_start (termo_t *tk);
int termo_stop (termo_t *tk);
int termo_is_started (termo_t *tk);

int termo_get_fd (termo_t *tk);

int termo_get_flags (termo_t *tk);
void termo_set_flags (termo_t *tk, int newflags);

int termo_get_waittime (termo_t *tk);
void termo_set_waittime (termo_t *tk, int msec);

int termo_get_canonflags (termo_t *tk);
void termo_set_canonflags (termo_t *tk, int flags);

size_t termo_get_buffer_size (termo_t *tk);
int termo_set_buffer_size (termo_t *tk, size_t size);

size_t termo_get_buffer_remaining (termo_t *tk);

void termo_canonicalise (termo_t *tk, termo_key_t *key);

int termo_get_mouse_proto (termo_t *tk);
int termo_set_mouse_proto (termo_t *tk, int proto);
int termo_guess_mouse_proto (termo_t *tk);

termo_mouse_tracking_t termo_get_mouse_tracking_mode (termo_t *tk);
int termo_set_mouse_tracking_mode (termo_t *tk, termo_mouse_tracking_t mode);

termo_result_t termo_getkey (termo_t *tk, termo_key_t *key);
termo_result_t termo_getkey_force (termo_t *tk, termo_key_t *key);
termo_result_t termo_waitkey (termo_t *tk, termo_key_t *key);

termo_result_t termo_advisereadable (termo_t *tk);

size_t termo_push_bytes (termo_t *tk, const char *bytes, size_t len);

termo_sym_t termo_register_keyname (termo_t *tk,
	termo_sym_t sym, const char *name);
const char *termo_get_keyname (termo_t *tk, termo_sym_t sym);
const char *termo_lookup_keyname (termo_t *tk,
	const char *str, termo_sym_t *sym);

termo_sym_t termo_keyname2sym (termo_t *tk, const char *keyname);

termo_result_t termo_interpret_mouse (termo_t *tk,
	const termo_key_t *key, termo_mouse_event_t *event,
	int *button, int *line, int *col);
termo_result_t termo_interpret_position (termo_t *tk,
	const termo_key_t *key, int *line, int *col);
termo_result_t termo_interpret_modereport (termo_t *tk,
	const termo_key_t *key, int *initial, int *mode, int *value);
termo_result_t termo_interpret_csi (termo_t *tk,
	const termo_key_t *key, long args[], size_t *nargs, unsigned long *cmd);

typedef enum termo_format termo_format_t;
enum termo_format
{
	// Shift-... instead of S-...
	TERMO_FORMAT_LONGMOD     = 1 << 0,
	// ^X instead of C-X
	TERMO_FORMAT_CARETCTRL   = 1 << 1,
	// Meta- or M- instead of Alt- or A-
	TERMO_FORMAT_ALTISMETA   = 1 << 2,
	// Wrap special keys in brackets like <Escape>
	TERMO_FORMAT_WRAPBRACKET = 1 << 3,
	// M Foo instead of M-Foo
	TERMO_FORMAT_SPACEMOD    = 1 << 4,
	// meta or m instead of Meta or M
	TERMO_FORMAT_LOWERMOD    = 1 << 5,
	// page down instead of PageDown
	TERMO_FORMAT_LOWERSPACE  = 1 << 6,
	// Include mouse position if relevant; @ col,line
	TERMO_FORMAT_MOUSE_POS   = 1 << 8
};

// Some useful combinations

#define TERMO_FORMAT_VIM (termo_format_t) \
	(TERMO_FORMAT_ALTISMETA | TERMO_FORMAT_WRAPBRACKET)
#define TERMO_FORMAT_URWID (termo_format_t) \
	(TERMO_FORMAT_LONGMOD | TERMO_FORMAT_ALTISMETA | \
	 TERMO_FORMAT_LOWERMOD | TERMO_FORMAT_SPACEMOD | \
	 TERMO_FORMAT_LOWERSPACE)

size_t termo_strfkey (termo_t *tk, char *buffer, size_t len,
	termo_key_t *key, termo_format_t format);
const char *termo_strpkey (termo_t *tk, const char *str,
	termo_key_t *key, termo_format_t format);

int termo_keycmp (termo_t *tk,
	const termo_key_t *key1, const termo_key_t *key2);

#endif  // ! TERMO_H


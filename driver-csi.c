#include "termo.h"
#include "termo-internal.h"

#include <stdio.h>
#include <string.h>

// There are 64 basic codes 0x40 - 0x7F that end a key sequence,
// plus 0x24 ($) for shifted keys in rxvt-based terminals.

// The CSI/SS3 naming isn't completely appropriate, as that only really applies
// to the other direction of communication, that is from the application
// to the terminal.  What the terminal sends back doesn't have to conform to
// ECMA-48 (and indeed doesn't, as rxvt's 0x24 ($) is out of the range).

static int keyinfo_initialised = 0;
static struct keyinfo ss3s[96];
static char ss3_kpalts[96];

typedef struct
{
	termo_t *tk;
}
termo_csi_t;

typedef termo_result_t (*csi_handler_fn)
	(termo_t *tk, termo_key_t *key, int cmd, long *arg, int args);
static csi_handler_fn csi_handlers[96];

//
// Handler for CSI/SS3 cmd keys
//

static struct keyinfo csi_ss3s[96];

static termo_result_t
handle_csi_ss3_full (termo_t *tk,
	termo_key_t *key, int cmd, long *arg, int args)
{
	(void) tk;

	if (args > 1 && arg[1] != -1)
		key->modifiers = arg[1] - 1;
	else
		key->modifiers = 0;

	key->type       =   csi_ss3s[cmd - 0x20].type;
	key->code.sym   =   csi_ss3s[cmd - 0x20].sym;
	key->modifiers &= ~(csi_ss3s[cmd - 0x20].modifier_mask);
	key->modifiers |=   csi_ss3s[cmd - 0x20].modifier_set;

	if (key->code.sym == TERMO_SYM_UNKNOWN)
		return TERMO_RES_NONE;
	return TERMO_RES_KEY;
}

static void
register_csi_ss3_full (termo_type_t type, termo_sym_t sym,
	int modifier_set, int modifier_mask, unsigned char cmd)
{
	if (cmd < 0x20 || cmd >= 0x80)
		return;

	csi_ss3s[cmd - 0x20].type          = type;
	csi_ss3s[cmd - 0x20].sym           = sym;
	csi_ss3s[cmd - 0x20].modifier_set  = modifier_set;
	csi_ss3s[cmd - 0x20].modifier_mask = modifier_mask;

	csi_handlers[cmd - 0x20] = &handle_csi_ss3_full;
}

static void
register_csi_ss3 (termo_type_t type, termo_sym_t sym, unsigned char cmd)
{
	register_csi_ss3_full (type, sym, 0, 0, cmd);
}

//
// Handler for regular cursor key codes
//

static termo_result_t
handle_csi_cursor (termo_t *tk,
	termo_key_t *key, int cmd, long *arg, int args)
{
	(void) tk;

	// CSI arrow keys without arguments are usually Ctrl-modified,
	// and if not, as is the case with urxvt, they're specified in terminfo.
	// In addition to that, xterm can specify modifiers in an argument.
	key->type = TERMO_TYPE_KEYSYM;
	if (args > 1 && arg[1] != -1)
		key->modifiers = arg[1] - 1;
	else
		key->modifiers = TERMO_KEYMOD_CTRL;

	if (cmd == 'A') key->code.sym = TERMO_SYM_UP;
	if (cmd == 'B') key->code.sym = TERMO_SYM_DOWN;
	if (cmd == 'C') key->code.sym = TERMO_SYM_RIGHT;
	if (cmd == 'D') key->code.sym = TERMO_SYM_LEFT;
	return TERMO_RES_KEY;
}

//
// Handler for rxvt special cursor key codes
//

static termo_result_t
handle_csi_cursor_rxvt (termo_t *tk,
	termo_key_t *key, int cmd, long *arg, int args)
{
	(void) tk;
	(void) arg;
	(void) args;

	// CSI with small letter stands for Shift
	// SS3 with small letter stands for Ctrl but we don't handle that here
	key->type = TERMO_TYPE_KEYSYM;
	key->modifiers = TERMO_KEYMOD_SHIFT;

	if (cmd == 'a') key->code.sym = TERMO_SYM_UP;
	if (cmd == 'b') key->code.sym = TERMO_SYM_DOWN;
	if (cmd == 'c') key->code.sym = TERMO_SYM_RIGHT;
	if (cmd == 'd') key->code.sym = TERMO_SYM_LEFT;
	return TERMO_RES_KEY;
}

//
// Handler for rxvt SS3-only key combinations
//

static void
register_ss3 (termo_type_t type, termo_sym_t sym,
	int modifier_set, unsigned char cmd)
{
	if (cmd < 0x20 || cmd >= 0x80)
		return;

	ss3s[cmd - 0x20].type          = type;
	ss3s[cmd - 0x20].sym           = sym;
	ss3s[cmd - 0x20].modifier_set  = modifier_set;
	ss3s[cmd - 0x20].modifier_mask = 0;
}

//
// Handler for SS3 keys with kpad alternate representations
//

static void
register_ss3kpalt (termo_type_t type, termo_sym_t sym,
	unsigned char cmd, char kpalt)
{
	if (cmd < 0x20 || cmd >= 0x80)
		return;

	ss3s[cmd - 0x20].type          = type;
	ss3s[cmd - 0x20].sym           = sym;
	ss3s[cmd - 0x20].modifier_set  = 0;
	ss3s[cmd - 0x20].modifier_mask = 0;

	ss3_kpalts[cmd - 0x20] = kpalt;
}

//
// Handler for CSI number ~ function keys
//

// This value must be increased if more CSI function keys are added
static struct keyinfo csifuncs[35];
#define NCSIFUNCS ((long) (sizeof csifuncs / sizeof csifuncs[0]))

static termo_result_t
handle_csifunc (termo_t *tk, termo_key_t *key, int cmd, long *arg, int args)
{
	(void) cmd;

	if (args > 1 && arg[1] != -1)
		key->modifiers = arg[1] - 1;
	else
		key->modifiers = 0;
	key->type = TERMO_TYPE_KEYSYM;

	if (arg[0] == 27)
	{
		int mod = key->modifiers;
		(*tk->method.emit_codepoint) (tk, arg[2], key);
		key->modifiers |= mod;
	}
	else if (arg[0] >= 0 && arg[0] < NCSIFUNCS)
	{
		key->type       =   csifuncs[arg[0]].type;
		key->code.sym   =   csifuncs[arg[0]].sym;
		key->modifiers &= ~(csifuncs[arg[0]].modifier_mask);
		key->modifiers |=   csifuncs[arg[0]].modifier_set;
	}
	else
		key->code.sym = TERMO_SYM_UNKNOWN;

	if (key->code.sym == TERMO_SYM_UNKNOWN)
	{
#ifdef DEBUG
		fprintf (stderr, "CSI: Unknown function key %ld\n", arg[0]);
#endif
		return TERMO_RES_NONE;
	}

	return TERMO_RES_KEY;
}

static void
register_csifunc (termo_type_t type, termo_sym_t sym, int number)
{
	if (number >= NCSIFUNCS)
		return;

	csifuncs[number].type          = type;
	csifuncs[number].sym           = sym;
	csifuncs[number].modifier_set  = 0;
	csifuncs[number].modifier_mask = 0;

	csi_handlers['~' - 0x20] = &handle_csifunc;
}

//
// rxvt seems to emit these instead of ~ when holding various modifiers
//

static termo_result_t
handle_csi_rxvt (termo_t *tk,
	termo_key_t *key, int cmd, long *arg, int args)
{
	termo_result_t res;
	switch (cmd)
	{
	case '^':
		res = handle_csifunc (tk, key, cmd, arg, args);
		if (res == TERMO_RES_KEY)
			key->modifiers |= TERMO_KEYMOD_CTRL;
		return res;
	case '$':
		res = handle_csifunc (tk, key, cmd, arg, args);
		if (res == TERMO_RES_KEY)
			key->modifiers |= TERMO_KEYMOD_SHIFT;
		return res;
	case '@':
		res = handle_csifunc (tk, key, cmd, arg, args);
		if (res == TERMO_RES_KEY)
			key->modifiers |= TERMO_KEYMOD_CTRL | TERMO_KEYMOD_SHIFT;
		return res;
	default:
		return TERMO_RES_NONE;
	}
}

//
// Handler for CSI u extended Unicode keys
//

static termo_result_t
handle_csi_u (termo_t *tk, termo_key_t *key, int cmd, long *arg, int args)
{
	switch (cmd)
	{
	case 'u':
	{
		if (args > 1 && arg[1] != -1)
			key->modifiers = arg[1] - 1;
		else
			key->modifiers = 0;

		int mod = key->modifiers;
		key->type = TERMO_TYPE_KEYSYM;
		(*tk->method.emit_codepoint) (tk, arg[0], key);
		key->modifiers |= mod;
		return TERMO_RES_KEY;
	}
	default:
		return TERMO_RES_NONE;
	}
}

//
// Handler for CSI M / CSI m mouse events in SGR and rxvt encodings
// Note: This does not handle X10 encoding
//

static termo_result_t
handle_csi_m (termo_t *tk, termo_key_t *key, int cmd, long *arg, int args)
{
	(void) tk;

	int initial = cmd >> 8;
	cmd &= 0xff;

	switch (cmd)
	{
	case 'M':
	case 'm':
		break;
	default:
		return TERMO_RES_NONE;
	}

	if (!initial && args >= 3)
	{
		// rxvt protocol
		key->type = TERMO_TYPE_MOUSE;
		key->code.mouse.info = arg[0] - 0x20;

		key->modifiers = (key->code.mouse.info & 0x1c) >> 2;
		key->code.mouse.info &= ~0x1c;

		termo_key_set_linecol (key, arg[2] - 1, arg[1] - 1);
		return TERMO_RES_KEY;
	}

	if (initial == '<' && args >= 3)
	{
		// SGR protocol
		key->type = TERMO_TYPE_MOUSE;
		key->code.mouse.info = arg[0];

		key->modifiers = (key->code.mouse.info & 0x1c) >> 2;
		key->code.mouse.info &= ~0x1c;

		termo_key_set_linecol (key, arg[2] - 1, arg[1] - 1);

		if (cmd == 'm')  // release
			key->code.mouse.info |= 0x8000;
		return TERMO_RES_KEY;
	}
	return TERMO_RES_NONE;
}

//
// Handler for CSI I / CSI O focus events
//

static termo_result_t
handle_csi_IO (termo_t *tk, termo_key_t *key, int cmd, long *arg, int args)
{
	(void) tk;
	(void) arg;
	(void) args;

	switch (cmd &= 0xff)
	{
	case 'I':
		key->type = TERMO_TYPE_FOCUS;
		key->code.focused = true;
		return TERMO_RES_KEY;
	case 'O':
		key->type = TERMO_TYPE_FOCUS;
		key->code.focused = false;
		return TERMO_RES_KEY;
	default:
		return TERMO_RES_NONE;
	}
	return TERMO_RES_NONE;
}

termo_result_t
termo_interpret_mouse (termo_t *tk, const termo_key_t *key,
	termo_mouse_event_t *event, int *button, int *line, int *col)
{
	(void) tk;

	if (key->type != TERMO_TYPE_MOUSE)
		return TERMO_RES_NONE;

	int btn = 0;
	int code = key->code.mouse.info;
	int drag = code & 0x20;
	code &= ~0x3c;

	termo_mouse_event_t ev;
	switch (code)
	{
	case 0:
	case 1:
	case 2:
		ev = drag ? TERMO_MOUSE_DRAG : TERMO_MOUSE_PRESS;
		btn = code + 1;
		break;

	case 3:
		ev = TERMO_MOUSE_RELEASE;
		// no button hint
		break;

	case 64:
	case 65:
		ev = drag ? TERMO_MOUSE_DRAG : TERMO_MOUSE_PRESS;
		btn = code + 4 - 64;
		break;

	default:
		ev = TERMO_MOUSE_UNKNOWN;
	}

	if (key->code.mouse.info & 0x8000)
		ev = TERMO_MOUSE_RELEASE;

	if (event)
		*event = ev;
	if (button)
		*button = btn;

	termo_key_get_linecol (key, line, col);
	return TERMO_RES_KEY;
}

//
// Handler for CSI ? R position reports
// A plain CSI R with no arguments is probably actually <F3>
//

static termo_result_t
handle_csi_R (termo_t *tk, termo_key_t *key, int cmd, long *arg, int args)
{
	switch (cmd)
	{
	case 'R' | '?' << 8:
		if (args < 2)
			return TERMO_RES_NONE;

		key->type = TERMO_TYPE_POSITION;
		termo_key_set_linecol (key, arg[0] - 1, arg[1] - 1);
		return TERMO_RES_KEY;

	default:
		return handle_csi_ss3_full (tk, key, cmd, arg, args);
	}
}

termo_result_t
termo_interpret_position (termo_t *tk,
	const termo_key_t *key, int *line, int *col)
{
	(void) tk;

	if (key->type != TERMO_TYPE_POSITION)
		return TERMO_RES_NONE;

	termo_key_get_linecol (key, line, col);
	return TERMO_RES_KEY;
}

//
// Handler for CSI $y mode status reports
//

static termo_result_t
handle_csi_y (termo_t *tk, termo_key_t *key, int cmd, long *arg, int args)
{
	(void) tk;

	switch (cmd)
	{
	case 'y' | '$' << 16:
	case 'y' | '$' << 16 | '?' << 8:
		if (args < 2)
			return TERMO_RES_NONE;

		key->type = TERMO_TYPE_MODEREPORT;
		key->code.mode.initial = (cmd >> 8);
		key->code.mode.mode = arg[0];
		key->code.mode.value = arg[1];
		return TERMO_RES_KEY;

	default:
		return TERMO_RES_NONE;
	}
}

termo_result_t
termo_interpret_modereport (termo_t *tk,
	const termo_key_t *key, int *initial, int *mode, int *value)
{
	(void) tk;

	if (key->type != TERMO_TYPE_MODEREPORT)
		return TERMO_RES_NONE;

	if (initial)
		*initial = key->code.mode.initial;
	if (mode)
		*mode = key->code.mode.mode;
	if (value)
		*value = key->code.mode.value;
	return TERMO_RES_KEY;
}

#define CHARAT(i) (tk->buffer[tk->buffstart + (i)])

static termo_result_t
parse_csi (termo_t *tk, size_t introlen, size_t *csi_len,
	long args[], size_t *nargs, unsigned long *commandp)
{
	// Specifically allowing the rxvt special character for shifted function
	// keys to end a CSI-like sequence, otherwise expecting ECMA-48-like input
	bool allow_dollar = true;

	size_t csi_end = introlen;
	while (csi_end < tk->buffcount)
	{
		unsigned char c = CHARAT (csi_end);
		if ((c >= 0x40 && c < 0x80) || (allow_dollar && c == '$'))
			break;

		// However just accepting the dollar as an end character would break
		// parsing DECRPM responses (mode reports).  We can work around this
		// ambiguity by making use of the fact that rxvt key sequences have
		// exactly one numeric argument and no initial byte.
		if (c < '0' || c > '9')
			allow_dollar = false;

		csi_end++;
	}

	if (csi_end >= tk->buffcount)
		return TERMO_RES_AGAIN;

	unsigned char cmd = CHARAT (csi_end);
	*commandp = cmd;

	char present = 0;
	int argi = 0;

	size_t p = introlen;

	// See if there is an initial byte
	if (CHARAT (p) >= '<' && CHARAT (p) <= '?')
	{
		*commandp |= (CHARAT (p) << 8);
		p++;
	}

	// Now attempt to parse out up number;number;... separated values
	for (; p < csi_end; p++)
	{
		unsigned char c = CHARAT (p);
		if (c >= '0' && c <= '9')
		{
			if (!present)
			{
				args[argi] = c - '0';
				present = 1;
			}
			else
				args[argi] = (args[argi] * 10) + c - '0';
		}
		else if (c == ';')
		{
			if (!present)
				args[argi] = -1;
			present = 0;
			argi++;

			if (argi > 16)
				break;
		}
		else if (c >= 0x20 && c <= 0x2f)
		{
			*commandp |= c << 16;
			break;
		}
	}

	if (present)
		argi++;

	*nargs = argi;
	*csi_len = csi_end + 1;
	return TERMO_RES_KEY;
}

termo_result_t
termo_interpret_csi (termo_t *tk, const termo_key_t *key,
	long args[], size_t *nargs, unsigned long *cmd)
{
	if (tk->hightide == 0)
		return TERMO_RES_NONE;
	if (key->type != TERMO_TYPE_UNKNOWN_CSI)
		return TERMO_RES_NONE;

	size_t dummy;
	return parse_csi (tk, 0, &dummy, args, nargs, cmd);
}

static int
register_keys (void)
{
	int i;
	for (i = 0; i < 96; i++)
	{
		csi_ss3s[i].sym = TERMO_SYM_UNKNOWN;
		ss3s[i].sym     = TERMO_SYM_UNKNOWN;
		ss3_kpalts[i] = 0;
	}
	for (i = 0; i < NCSIFUNCS; i++)
		csifuncs[i].sym = TERMO_SYM_UNKNOWN;

	// Cursor keys handling; there's some weird, weird stuff going on here:
	//
	// rxvt-based terminals should output SS3 A/B/C/D for C-S-arrow keys;
	// rxvt-unicode however only seems to output Shift codes.
	//
	// xterm, PuTTY, tmux output SS3 A/B/C/D for normal arrow keys when
	// terminfo start string has been written -- it gets eaten by the terminfo
	// driver then, usually.  To the contrary, CSI A/B/C/D is used for Ctrl.

	register_ss3 (TERMO_TYPE_KEYSYM, TERMO_SYM_UP,    0, 'A');
	register_ss3 (TERMO_TYPE_KEYSYM, TERMO_SYM_DOWN,  0, 'B');
	register_ss3 (TERMO_TYPE_KEYSYM, TERMO_SYM_RIGHT, 0, 'C');
	register_ss3 (TERMO_TYPE_KEYSYM, TERMO_SYM_LEFT,  0, 'D');

	csi_handlers['A' - 0x20] = &handle_csi_cursor;
	csi_handlers['B' - 0x20] = &handle_csi_cursor;
	csi_handlers['C' - 0x20] = &handle_csi_cursor;
	csi_handlers['D' - 0x20] = &handle_csi_cursor;

	// Handle Shift-modified rxvt cursor keys (CSI a, CSI b, CSI c, CSI d)
	csi_handlers['a' - 0x20] = &handle_csi_cursor_rxvt;
	csi_handlers['b' - 0x20] = &handle_csi_cursor_rxvt;
	csi_handlers['c' - 0x20] = &handle_csi_cursor_rxvt;
	csi_handlers['d' - 0x20] = &handle_csi_cursor_rxvt;

	// Handle Ctrl-modified rxvt cursor keys (SS3 a, SS3 b, SS3 c, SS3 d)
	register_ss3 (TERMO_TYPE_KEYSYM, TERMO_SYM_UP,    TERMO_KEYMOD_CTRL, 'a');
	register_ss3 (TERMO_TYPE_KEYSYM, TERMO_SYM_DOWN,  TERMO_KEYMOD_CTRL, 'b');
	register_ss3 (TERMO_TYPE_KEYSYM, TERMO_SYM_RIGHT, TERMO_KEYMOD_CTRL, 'c');
	register_ss3 (TERMO_TYPE_KEYSYM, TERMO_SYM_LEFT,  TERMO_KEYMOD_CTRL, 'd');

	// End of cursor keys handling

	register_csi_ss3 (TERMO_TYPE_KEYSYM, TERMO_SYM_BEGIN, 'E');
	register_csi_ss3 (TERMO_TYPE_KEYSYM, TERMO_SYM_END,   'F');
	register_csi_ss3 (TERMO_TYPE_KEYSYM, TERMO_SYM_HOME,  'H');
	register_csi_ss3 (TERMO_TYPE_FUNCTION, 1, 'P');
	register_csi_ss3 (TERMO_TYPE_FUNCTION, 2, 'Q');
	register_csi_ss3 (TERMO_TYPE_FUNCTION, 3, 'R');
	register_csi_ss3 (TERMO_TYPE_FUNCTION, 4, 'S');

	register_csi_ss3_full (TERMO_TYPE_KEYSYM, TERMO_SYM_TAB,
		TERMO_KEYMOD_SHIFT, TERMO_KEYMOD_SHIFT, 'Z');

	register_ss3kpalt (TERMO_TYPE_KEYSYM, TERMO_SYM_KPENTER,  'M', 0);
	register_ss3kpalt (TERMO_TYPE_KEYSYM, TERMO_SYM_KPEQUALS, 'X', '=');
	register_ss3kpalt (TERMO_TYPE_KEYSYM, TERMO_SYM_KPMULT,   'j', '*');
	register_ss3kpalt (TERMO_TYPE_KEYSYM, TERMO_SYM_KPPLUS,   'k', '+');
	register_ss3kpalt (TERMO_TYPE_KEYSYM, TERMO_SYM_KPCOMMA,  'l', ',');
	register_ss3kpalt (TERMO_TYPE_KEYSYM, TERMO_SYM_KPMINUS,  'm', '-');
	register_ss3kpalt (TERMO_TYPE_KEYSYM, TERMO_SYM_KPPERIOD, 'n', '.');
	register_ss3kpalt (TERMO_TYPE_KEYSYM, TERMO_SYM_KPDIV,    'o', '/');
	register_ss3kpalt (TERMO_TYPE_KEYSYM, TERMO_SYM_KP0,      'p', '0');
	register_ss3kpalt (TERMO_TYPE_KEYSYM, TERMO_SYM_KP1,      'q', '1');
	register_ss3kpalt (TERMO_TYPE_KEYSYM, TERMO_SYM_KP2,      'r', '2');
	register_ss3kpalt (TERMO_TYPE_KEYSYM, TERMO_SYM_KP3,      's', '3');
	register_ss3kpalt (TERMO_TYPE_KEYSYM, TERMO_SYM_KP4,      't', '4');
	register_ss3kpalt (TERMO_TYPE_KEYSYM, TERMO_SYM_KP5,      'u', '5');
	register_ss3kpalt (TERMO_TYPE_KEYSYM, TERMO_SYM_KP6,      'v', '6');
	register_ss3kpalt (TERMO_TYPE_KEYSYM, TERMO_SYM_KP7,      'w', '7');
	register_ss3kpalt (TERMO_TYPE_KEYSYM, TERMO_SYM_KP8,      'x', '8');
	register_ss3kpalt (TERMO_TYPE_KEYSYM, TERMO_SYM_KP9,      'y', '9');

	register_csifunc (TERMO_TYPE_KEYSYM, TERMO_SYM_FIND,      1);
	register_csifunc (TERMO_TYPE_KEYSYM, TERMO_SYM_INSERT,    2);
	register_csifunc (TERMO_TYPE_KEYSYM, TERMO_SYM_DELETE,    3);
	register_csifunc (TERMO_TYPE_KEYSYM, TERMO_SYM_SELECT,    4);
	register_csifunc (TERMO_TYPE_KEYSYM, TERMO_SYM_PAGEUP,    5);
	register_csifunc (TERMO_TYPE_KEYSYM, TERMO_SYM_PAGEDOWN,  6);
	register_csifunc (TERMO_TYPE_KEYSYM, TERMO_SYM_HOME,      7);
	register_csifunc (TERMO_TYPE_KEYSYM, TERMO_SYM_END,       8);

	register_csifunc (TERMO_TYPE_FUNCTION, 1,  11);
	register_csifunc (TERMO_TYPE_FUNCTION, 2,  12);
	register_csifunc (TERMO_TYPE_FUNCTION, 3,  13);
	register_csifunc (TERMO_TYPE_FUNCTION, 4,  14);
	register_csifunc (TERMO_TYPE_FUNCTION, 5,  15);
	register_csifunc (TERMO_TYPE_FUNCTION, 6,  17);
	register_csifunc (TERMO_TYPE_FUNCTION, 7,  18);
	register_csifunc (TERMO_TYPE_FUNCTION, 8,  19);
	register_csifunc (TERMO_TYPE_FUNCTION, 9,  20);
	register_csifunc (TERMO_TYPE_FUNCTION, 10, 21);
	register_csifunc (TERMO_TYPE_FUNCTION, 11, 23);
	register_csifunc (TERMO_TYPE_FUNCTION, 12, 24);
	register_csifunc (TERMO_TYPE_FUNCTION, 13, 25);
	register_csifunc (TERMO_TYPE_FUNCTION, 14, 26);
	register_csifunc (TERMO_TYPE_FUNCTION, 15, 28);
	register_csifunc (TERMO_TYPE_FUNCTION, 16, 29);
	register_csifunc (TERMO_TYPE_FUNCTION, 17, 31);
	register_csifunc (TERMO_TYPE_FUNCTION, 18, 32);
	register_csifunc (TERMO_TYPE_FUNCTION, 19, 33);
	register_csifunc (TERMO_TYPE_FUNCTION, 20, 34);

	csi_handlers['u' - 0x20] = &handle_csi_u;

	csi_handlers['M' - 0x20] = &handle_csi_m;
	csi_handlers['m' - 0x20] = &handle_csi_m;

	csi_handlers['I' - 0x20] = &handle_csi_IO;
	csi_handlers['O' - 0x20] = &handle_csi_IO;

	csi_handlers['R' - 0x20] = &handle_csi_R;

	csi_handlers['y' - 0x20] = &handle_csi_y;

	csi_handlers['^' - 0x20] = &handle_csi_rxvt;
	csi_handlers['$' - 0x20] = &handle_csi_rxvt;
	csi_handlers['@' - 0x20] = &handle_csi_rxvt;

	keyinfo_initialised = 1;
	return 1;
}

static void *
new_driver (termo_t *tk, const char *term)
{
	(void) term;

	if (!keyinfo_initialised && !register_keys ())
		return NULL;

	termo_csi_t *csi = malloc (sizeof *csi);
	if (!csi)
		return NULL;

	csi->tk = tk;
	return csi;
}

static void
free_driver (void *info)
{
	termo_csi_t *csi = info;
	free (csi);
}

static termo_result_t
peekkey_csi (termo_t *tk, termo_csi_t *csi,
	size_t introlen, termo_key_t *key, int flags, size_t *nbytep)
{
	(void) csi;

	size_t csi_len;
	size_t args = 16;
	long arg[16];
	unsigned long cmd;

	termo_result_t ret = parse_csi (tk, introlen, &csi_len, arg, &args, &cmd);
	if (ret == TERMO_RES_AGAIN)
	{
		if (!(flags & PEEKKEY_FORCE))
			return TERMO_RES_AGAIN;

		(*tk->method.emit_codepoint) (tk, '[', key);
		key->modifiers |= TERMO_KEYMOD_ALT;
		*nbytep = introlen;
		return TERMO_RES_KEY;
	}

	// Mouse in X10 encoding consumes the next 3 bytes also (or more with 1005)
	if (cmd == 'M' && args < 3)
	{
		tk->buffstart += csi_len;
		tk->buffcount -= csi_len;

		termo_result_t mouse_result =
			(*tk->method.peekkey_mouse) (tk, key, nbytep);

		tk->buffstart -= csi_len;
		tk->buffcount += csi_len;

		if (mouse_result == TERMO_RES_KEY)
			*nbytep += csi_len;
		return mouse_result;
	}

	termo_result_t result = TERMO_RES_NONE;

	// We know from the logic above that cmd must be >= 0x20 and < 0x80
	if (csi_handlers[(cmd & 0xff) - 0x20])
		result = (*csi_handlers[(cmd & 0xff) - 0x20]) (tk, key, cmd, arg, args);

	if (result == TERMO_RES_NONE)
	{
#ifdef DEBUG
		switch (args)
		{
		case 1:
			fprintf (stderr, "CSI: Unknown arg1=%ld cmd=%c\n",
				arg[0], (char) cmd);
			break;
		case 2:
			fprintf (stderr, "CSI: Unknown arg1=%ld arg2=%ld cmd=%c\n",
				arg[0], arg[1], (char) cmd);
			break;
		case 3:
			fprintf (stderr, "CSI: Unknown arg1=%ld arg2=%ld arg3=%ld cmd=%c\n",
				arg[0], arg[1], arg[2], (char) cmd);
			break;
		default:
			fprintf (stderr, "CSI: Unknown arg1=%ld arg2=%ld arg3=%ld ... "
				"args=%zu cmd=%c\n", arg[0], arg[1], arg[2], args, (char) cmd);
			break;
		}
#endif
		key->type = TERMO_TYPE_UNKNOWN_CSI;
		key->code.number = cmd;

		tk->hightide = csi_len - introlen;
		*nbytep = introlen; // Do not yet eat the data bytes
		return TERMO_RES_KEY;
	}

	*nbytep = csi_len;
	return result;
}

static termo_result_t
peekkey_ss3 (termo_t *tk, termo_csi_t *csi, size_t introlen,
	termo_key_t *key, int flags, size_t *nbytep)
{
	(void) csi;

	if (tk->buffcount < introlen + 1)
	{
		if (!(flags & PEEKKEY_FORCE))
			return TERMO_RES_AGAIN;

		(*tk->method.emit_codepoint) (tk, 'O', key);
		key->modifiers |= TERMO_KEYMOD_ALT;
		*nbytep = tk->buffcount;
		return TERMO_RES_KEY;
	}

	unsigned char cmd = CHARAT (introlen);

	if (cmd < 0x40 || cmd >= 0x80)
		return TERMO_RES_NONE;

	// First go have a look at SS3-only sequences
	key->type      = ss3s[cmd - 0x20].type;
	key->code.sym  = ss3s[cmd - 0x20].sym;
	key->modifiers = ss3s[cmd - 0x20].modifier_set;

	// If that fails, try our mixed table (is there a reason for it?)
	if (key->code.sym == TERMO_SYM_UNKNOWN)
	{
		key->type      = csi_ss3s[cmd - 0x20].type;
		key->code.sym  = csi_ss3s[cmd - 0x20].sym;
		key->modifiers = csi_ss3s[cmd - 0x20].modifier_set;
	}
	// If we have a match for a keypad key but user wants to receive them
	// as characters instead, convert them
	else if ((tk->flags & TERMO_FLAG_CONVERTKP && ss3_kpalts[cmd - 0x20]))
	{
		key->type = TERMO_TYPE_KEY;
		key->code.codepoint = ss3_kpalts[cmd - 0x20];
		key->modifiers = 0;

		key->multibyte[0] = key->code.codepoint;
		key->multibyte[1] = 0;
	}

	if (key->code.sym == TERMO_SYM_UNKNOWN)
	{
#ifdef DEBUG
		fprintf (stderr, "CSI: Unknown SS3 %c (0x%02x)\n", (char) cmd, cmd);
#endif
		return TERMO_RES_NONE;
	}

	*nbytep = introlen + 1;
	return TERMO_RES_KEY;
}

static termo_result_t
peekkey (termo_t *tk, void *info,
	termo_key_t *key, int flags, size_t *nbytep)
{
	if (tk->buffcount == 0)
		return tk->is_closed ? TERMO_RES_EOF : TERMO_RES_NONE;

	termo_csi_t *csi = info;

	// Now we're sure at least 1 byte is valid
	unsigned char b0 = CHARAT (0);

	if (b0 == 0x1b && tk->buffcount == 1)
		return TERMO_RES_AGAIN;
	if (b0 == 0x1b && tk->buffcount > 1 && CHARAT (1) == '[')
		return peekkey_csi (tk, csi, 2, key, flags, nbytep);
	if (b0 == 0x1b && tk->buffcount > 1 && CHARAT (1) == 'O')
		return peekkey_ss3 (tk, csi, 2, key, flags, nbytep);
	if (b0 == 0x8f)
		return peekkey_ss3 (tk, csi, 1, key, flags, nbytep);
	if (b0 == 0x9b)
		return peekkey_csi (tk, csi, 1, key, flags, nbytep);
	return TERMO_RES_NONE;
}

termo_driver_t termo_driver_csi =
{
	.name        = "CSI",
	.new_driver  = new_driver,
	.free_driver = free_driver,
	.peekkey     = peekkey,
};

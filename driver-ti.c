// We want strdup()
#define _XOPEN_SOURCE 600

#include "termo.h"
#include "termo-internal.h"

#ifdef HAVE_UNIBILIUM
# include <unibilium.h>
#else
# include <curses.h>
# include <term.h>
#endif

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

// To be efficient at lookups, we store the byte sequence => keyinfo mapping
// in a trie. This avoids a slow linear search through a flat list of
// sequences. Because it is likely most nodes will be very sparse, we optimise
// vector to store an extent map after the database is loaded.

typedef enum
{
	TYPE_KEY,
	TYPE_ARRAY,
	TYPE_MOUSE,
}
trie_nodetype_t;

typedef struct trie_node
{
	trie_nodetype_t type;
}
trie_node_t;

typedef struct trie_node_key
{
	trie_nodetype_t type;
	keyinfo_t key;
}
trie_node_key_t;

typedef struct trie_node_array
{
	trie_nodetype_t type;
	unsigned char min, max; // INCLUSIVE endpoints of the extent range
	trie_node_t *arr[];     // dynamic size at allocation time
}
trie_node_array_t;

typedef struct
{
	termo_t *tk;
	trie_node_t *root;

	char *start_string;
	char *stop_string;

	bool have_mouse;
	char *set_mouse_string;
}
termo_ti_t;

static int funcname2keysym (const char *funcname, termo_type_t *typep,
	termo_sym_t *symp, int *modmask, int *modsetp);
static int insert_seq (termo_ti_t *ti, const char *seq, trie_node_t *node);

static trie_node_t *
new_node_key (termo_type_t type, termo_sym_t sym, int modmask, int modset)
{
	trie_node_key_t *n = malloc (sizeof *n);
	if (!n)
		return NULL;

	n->type = TYPE_KEY;
	n->key.type = type;
	n->key.sym  = sym;
	n->key.modifier_mask = modmask;
	n->key.modifier_set  = modset;
	return (trie_node_t *) n;
}

static trie_node_t *
new_node_arr (unsigned char min, unsigned char max)
{
	trie_node_array_t *n = malloc (sizeof *n
		+ ((int) max - min + 1) * sizeof n->arr[0]);
	if (!n)
		return NULL;

	n->type = TYPE_ARRAY;
	n->min = min;
	n->max = max;

	int i;
	for (i = min; i <= max; i++)
		n->arr[i - min] = NULL;

	return (trie_node_t *) n;
}

static trie_node_t *
lookup_next (trie_node_t *n, unsigned char b)
{
	switch (n->type)
	{
	case TYPE_KEY:
	case TYPE_MOUSE:
		fprintf (stderr, "fatal: lookup_next within a TYPE_KEY node\n");
		abort ();
	case TYPE_ARRAY:
	{
		trie_node_array_t *nar = (trie_node_array_t *) n;
		if (b < nar->min || b > nar->max)
			return NULL;
		return nar->arr[b - nar->min];
	}
	}
	return NULL;  // Never reached but keeps compiler happy
}

static void
free_trie (trie_node_t *n)
{
	switch (n->type)
	{
	case TYPE_KEY:
	case TYPE_MOUSE:
		break;
	case TYPE_ARRAY:
	{
		trie_node_array_t *nar = (trie_node_array_t *) n;
		int i;
		for (i = nar->min; i <= nar->max; i++)
			if (nar->arr[i - nar->min])
				free_trie (nar->arr[i - nar->min]);
	}
	}
	free (n);
}

static trie_node_t *
compress_trie (struct trie_node *n)
{
	if (!n)
		return NULL;

	switch (n->type)
	{
	case TYPE_KEY:
	case TYPE_MOUSE:
		return n;
	case TYPE_ARRAY:
	{
		trie_node_array_t *nar = (trie_node_array_t *) n;
		// Find the real bounds
		unsigned char min, max;
		for (min = 0; !nar->arr[min]; min++)
			;
		for (max = 0xff; !nar->arr[max]; max--)
			;

		trie_node_array_t *new = (trie_node_array_t *) new_node_arr (min, max);
		int i;
		for (i = min; i <= max; i++)
			new->arr[i - min] = compress_trie (nar->arr[i]);

		free (nar);
		return (trie_node_t *) new;
	}
	}
	return n;
}

static bool
load_terminfo (termo_ti_t *ti, const char *term)
{
	const char *mouse_report_string = NULL;

#ifdef HAVE_UNIBILIUM
	unibi_term *unibi = unibi_from_term (term);
	if (!unibi)
		return false;

	for (int i = unibi_string_begin_ + 1; i < unibi_string_end_; i++)
	{
		const char *name = unibi_name_str (i);
		const char *value = unibi_get_str (unibi, i);
#else
	// Have to cast away the const. But it's OK - we know terminfo won't
	// really modify term
	int err;
	if (setupterm ((char *) term, 1, &err) != OK)
		return false;

	for (int i = 0; strfnames[i]; i++)
	{
		const char *name = strfnames[i];
		const char *value = tigetstr (strnames[i]);
#endif
		// Only care about the key_* constants
		if (strncmp (name, "key_", 4) != 0)
			continue;
		if (!value || value == (char*) -1)
			continue;

		trie_node_t *node = NULL;
		if (!strcmp (name + 4, "mouse"))
			mouse_report_string = value;
		else
		{
			termo_type_t type;
			termo_sym_t sym;
			int mask = 0;
			int set = 0;

			if (!funcname2keysym (name + 4, &type, &sym, &mask, &set))
				continue;

			if (sym == TERMO_SYM_NONE)
				continue;

			node = new_node_key (type, sym, mask, set);
		}

		if (node && !insert_seq (ti, value, node))
		{
			free (node);
			return false;
		}
	}

	// Clone the behaviour of ncurses for xterm mouse support
#ifdef HAVE_UNIBILIUM
	const char *set_mouse_string = unibi_get_str (unibi, "XM");
#else
	const char *set_mouse_string = tigetstr ("XM");
#endif
	if (!set_mouse_string || set_mouse_string == (char *) -1)
		ti->set_mouse_string = strdup ("\E[?1000%?%p1%{1}%=%th%el%;");
	else
		ti->set_mouse_string = strdup (set_mouse_string);

	if (!mouse_report_string && strstr (term, "xterm"))
		mouse_report_string = "\x1b[M";
	if (mouse_report_string)
	{
		ti->have_mouse = true;

		trie_node_t *node = malloc (sizeof *node);
		if (!node)
			return false;

		node->type = TYPE_MOUSE;
		if (!insert_seq (ti, mouse_report_string, node))
		{
			free (node);
			return false;
		}
	}

	if (!ti->have_mouse)
		ti->tk->guessed_mouse_proto = TERMO_MOUSE_PROTO_NONE;
	else if (strstr (term, "rxvt") == term)
		// urxvt generally doesn't understand the SGR protocol.
		ti->tk->guessed_mouse_proto = TERMO_MOUSE_PROTO_RXVT;
	else
		// SGR (1006) is the superior protocol.  If it's not supported by the
		// terminal, nothing much happens and we continue getting events via
		// the original protocol (1000).  We can't afford to enable the UTF-8
		// protocol (1005) because it collides with the original (1000) and we
		// have no way of knowing if it's supported by the terminal.  Also both
		// 1000 and 1005 are broken in that they may produce characters that
		// are illegal in the current locale's charset.
		ti->tk->guessed_mouse_proto = TERMO_MOUSE_PROTO_SGR;

	// Take copies of these terminfo strings, in case we build multiple termo
	// instances for multiple different termtypes, and it's different by the
	// time we want to use it
#ifdef HAVE_UNIBILIUM
	const char *keypad_xmit = unibi_get_str (unibi, unibi_pkey_xmit);
#endif

	if (keypad_xmit)
		ti->start_string = strdup (keypad_xmit);
	else
		ti->start_string = NULL;

#ifdef HAVE_UNIBILIUM
	const char *keypad_local = unibi_get_str (unibi, unibi_pkey_local);
#endif

	if (keypad_local)
		ti->stop_string = strdup (keypad_local);
	else
		ti->stop_string = NULL;

#ifdef HAVE_UNIBILIUM
	unibi_destroy (unibi);
#endif

	return true;
}

static bool
write_string (termo_t *tk, char *string)
{
	if (tk->fd == -1 || !isatty (tk->fd) || !string)
		return true;

	// The terminfo database will contain keys in application cursor key mode.
	// We may need to enable that mode

	// Can't call putp or tputs because they suck and don't give us fd control
	size_t len = strlen (string);
	while (len)
	{
		ssize_t written = write (tk->fd, string, len);
		if (written == -1)
			return false;
		string += written;
		len -= written;
	}
	return true;
}

static bool
set_mouse (termo_ti_t *ti, bool enable)
{
#ifdef HAVE_UNIBILIUM
	unibi_var_t params[9] = { enable, 0, 0, 0, 0, 0, 0, 0, 0 };
	char start_string[unibi_run (ti->set_mouse_string, params, NULL, 0)];
	unibi_run (ti->set_mouse_string, params,
		start_string, sizeof start_string);
#else
	char *start_string = tparm (ti->set_mouse_string,
		enable, 0, 0, 0, 0, 0, 0, 0, 0);
#endif
	return write_string (ti->tk, start_string);
}

static bool
mouse_reset (termo_ti_t *ti)
{
	// Disable everything, a de-facto reset for all terminal mouse protocols
	return set_mouse (ti, false)
		&& write_string (ti->tk, "\E[?1002l")
		&& write_string (ti->tk, "\E[?1003l")

		&& write_string (ti->tk, "\E[?1005l")
		&& write_string (ti->tk, "\E[?1006l")
		&& write_string (ti->tk, "\E[?1015l");
}

static bool
mouse_set_proto (void *data, int proto, bool enable)
{
	termo_ti_t *ti = data;
	bool success = true;
	if (proto & TERMO_MOUSE_PROTO_VT200)
		success &= set_mouse (ti, enable);
	if (proto & TERMO_MOUSE_PROTO_UTF8)
		success &= write_string (ti->tk, enable ? "\E[?1005h" : "\E[?1005l");
	if (proto & TERMO_MOUSE_PROTO_SGR)
		success &= write_string (ti->tk, enable ? "\E[?1006h" : "\E[?1006l");
	if (proto & TERMO_MOUSE_PROTO_RXVT)
		success &= write_string (ti->tk, enable ? "\E[?1015h" : "\E[?1015l");
	return success;
}

static bool
mouse_set_tracking_mode (void *data,
	termo_mouse_tracking_t tracking, bool enable)
{
	termo_ti_t *ti = data;
	if (tracking == TERMO_MOUSE_TRACKING_DRAG)
		return write_string (ti->tk, enable ? "\E[?1002h" : "\E[?1002l");
	if (tracking == TERMO_MOUSE_TRACKING_ANY)
		return write_string (ti->tk, enable ? "\E[?1003h" : "\E[?1003l");
	return true;
}

static int
start_driver (termo_t *tk, void *info)
{
	termo_ti_t *ti = info;
	return write_string (tk, ti->start_string)
		&& ((!ti->have_mouse && tk->mouse_proto == TERMO_MOUSE_PROTO_NONE)
			|| mouse_reset (ti)) // We can never be sure
		&& mouse_set_proto (ti, tk->mouse_proto, true)
		&& mouse_set_tracking_mode (ti, tk->mouse_tracking, true);
}

static int
stop_driver (termo_t *tk, void *info)
{
	termo_ti_t *ti = info;
	return write_string (tk, ti->stop_string)
		&& mouse_set_proto (ti, tk->mouse_proto, false)
		&& mouse_set_tracking_mode (ti, tk->mouse_tracking, false);
}

static void *
new_driver (termo_t *tk, const char *term)
{
	termo_ti_t *ti = calloc (1, sizeof *ti);
	if (!ti)
		return NULL;

	ti->tk = tk;
	ti->root = new_node_arr (0, 0xff);
	if (!ti->root)
		goto abort_free_ti;

	if (!load_terminfo (ti, term))
		goto abort_free_trie;

	ti->root = compress_trie (ti->root);

	tk->ti_data = ti;
	tk->ti_method.set_mouse_proto = mouse_set_proto;
	tk->ti_method.set_mouse_tracking_mode = mouse_set_tracking_mode;
	return ti;

abort_free_trie:
	free_trie (ti->root);

abort_free_ti:
	free (ti);
	return NULL;
}

static void
free_driver (void *info)
{
	termo_ti_t *ti = info;
	ti->tk->ti_data = NULL;
	ti->tk->ti_method.set_mouse_proto = NULL;
	ti->tk->ti_method.set_mouse_tracking_mode = NULL;

	free_trie (ti->root);
	free (ti->set_mouse_string);
	free (ti->start_string);
	free (ti->stop_string);
	free (ti);
}

#define CHARAT(i) (tk->buffer[tk->buffstart + (i)])

static termo_result_t
peekkey (termo_t *tk, void *info,
	termo_key_t *key, int force, size_t *nbytep)
{
	termo_ti_t *ti = info;

	if (tk->buffcount == 0)
		return tk->is_closed ? TERMO_RES_EOF : TERMO_RES_NONE;

	trie_node_t *p = ti->root;
	unsigned int pos = 0;
	while (pos < tk->buffcount)
	{
		p = lookup_next (p, CHARAT (pos));
		if (!p)
			break;

		pos++;

		if (p->type == TYPE_KEY)
		{
			trie_node_key_t *nk = (trie_node_key_t *) p;
			key->type      = nk->key.type;
			key->code.sym  = nk->key.sym;
			key->modifiers = nk->key.modifier_set;
			*nbytep = pos;
			return TERMO_RES_KEY;
		}
		else if (p->type == TYPE_MOUSE)
		{
			tk->buffstart += pos;
			tk->buffcount -= pos;

			termo_result_t mouse_result =
				(*tk->method.peekkey_mouse) (tk, key, nbytep);

			tk->buffstart -= pos;
			tk->buffcount += pos;

			if (mouse_result == TERMO_RES_KEY)
				*nbytep += pos;

			return mouse_result;
		}
	}

	// If p is not NULL then we hadn't walked off the end yet, so we have a
	// partial match
	if (p && !force)
		return TERMO_RES_AGAIN;

	return TERMO_RES_NONE;
}

static struct func
{
	const char *funcname;
	termo_type_t type;
	termo_sym_t sym;
	int mods;
}
funcs[] =
{
	// THIS LIST MUST REMAIN SORTED!
	{ "backspace", TERMO_TYPE_KEYSYM, TERMO_SYM_BACKSPACE, 0 },
	{ "begin",     TERMO_TYPE_KEYSYM, TERMO_SYM_BEGIN,     0 },
	{ "beg",       TERMO_TYPE_KEYSYM, TERMO_SYM_BEGIN,     0 },
	{ "btab",      TERMO_TYPE_KEYSYM, TERMO_SYM_TAB,       TERMO_KEYMOD_SHIFT },
	{ "cancel",    TERMO_TYPE_KEYSYM, TERMO_SYM_CANCEL,    0 },
	{ "clear",     TERMO_TYPE_KEYSYM, TERMO_SYM_CLEAR,     0 },
	{ "close",     TERMO_TYPE_KEYSYM, TERMO_SYM_CLOSE,     0 },
	{ "command",   TERMO_TYPE_KEYSYM, TERMO_SYM_COMMAND,   0 },
	{ "copy",      TERMO_TYPE_KEYSYM, TERMO_SYM_COPY,      0 },
	{ "dc",        TERMO_TYPE_KEYSYM, TERMO_SYM_DELETE,    0 },
	{ "down",      TERMO_TYPE_KEYSYM, TERMO_SYM_DOWN,      0 },
	{ "end",       TERMO_TYPE_KEYSYM, TERMO_SYM_END,       0 },
	{ "enter",     TERMO_TYPE_KEYSYM, TERMO_SYM_ENTER,     0 },
	{ "exit",      TERMO_TYPE_KEYSYM, TERMO_SYM_EXIT,      0 },
	{ "find",      TERMO_TYPE_KEYSYM, TERMO_SYM_FIND,      0 },
	{ "help",      TERMO_TYPE_KEYSYM, TERMO_SYM_HELP,      0 },
	{ "home",      TERMO_TYPE_KEYSYM, TERMO_SYM_HOME,      0 },
	{ "ic",        TERMO_TYPE_KEYSYM, TERMO_SYM_INSERT,    0 },
	{ "left",      TERMO_TYPE_KEYSYM, TERMO_SYM_LEFT,      0 },
	{ "mark",      TERMO_TYPE_KEYSYM, TERMO_SYM_MARK,      0 },
	{ "message",   TERMO_TYPE_KEYSYM, TERMO_SYM_MESSAGE,   0 },
	{ "mouse",     TERMO_TYPE_KEYSYM, TERMO_SYM_NONE,      0 },
	{ "move",      TERMO_TYPE_KEYSYM, TERMO_SYM_MOVE,      0 },
	// Not quite, but it's the best we can do
	{ "next",      TERMO_TYPE_KEYSYM, TERMO_SYM_PAGEDOWN,  0 },
	{ "npage",     TERMO_TYPE_KEYSYM, TERMO_SYM_PAGEDOWN,  0 },
	{ "open",      TERMO_TYPE_KEYSYM, TERMO_SYM_OPEN,      0 },
	{ "options",   TERMO_TYPE_KEYSYM, TERMO_SYM_OPTIONS,   0 },
	{ "ppage",     TERMO_TYPE_KEYSYM, TERMO_SYM_PAGEUP,    0 },
	// Not quite, but it's the best we can do
	{ "previous",  TERMO_TYPE_KEYSYM, TERMO_SYM_PAGEUP,    0 },
	{ "print",     TERMO_TYPE_KEYSYM, TERMO_SYM_PRINT,     0 },
	{ "redo",      TERMO_TYPE_KEYSYM, TERMO_SYM_REDO,      0 },
	{ "reference", TERMO_TYPE_KEYSYM, TERMO_SYM_REFERENCE, 0 },
	{ "refresh",   TERMO_TYPE_KEYSYM, TERMO_SYM_REFRESH,   0 },
	{ "replace",   TERMO_TYPE_KEYSYM, TERMO_SYM_REPLACE,   0 },
	{ "restart",   TERMO_TYPE_KEYSYM, TERMO_SYM_RESTART,   0 },
	{ "resume",    TERMO_TYPE_KEYSYM, TERMO_SYM_RESUME,    0 },
	{ "right",     TERMO_TYPE_KEYSYM, TERMO_SYM_RIGHT,     0 },
	{ "save",      TERMO_TYPE_KEYSYM, TERMO_SYM_SAVE,      0 },
	{ "select",    TERMO_TYPE_KEYSYM, TERMO_SYM_SELECT,    0 },
	{ "suspend",   TERMO_TYPE_KEYSYM, TERMO_SYM_SUSPEND,   0 },
	{ "undo",      TERMO_TYPE_KEYSYM, TERMO_SYM_UNDO,      0 },
	{ "up",        TERMO_TYPE_KEYSYM, TERMO_SYM_UP,        0 },
	{ NULL },
};

static int
func_compare (const void *key, const void *element)
{
	return strcmp (key, ((struct func *) element)->funcname);
}

static int
funcname2keysym (const char *funcname,
	termo_type_t *typep, termo_sym_t *symp, int *modmaskp, int *modsetp)
{
	struct func *func = bsearch (funcname, funcs,
		sizeof funcs / sizeof funcs[0], sizeof funcs[0], func_compare);
	if (func)
	{
		*typep    = func->type;
		*symp     = func->sym;
		*modmaskp = func->mods;
		*modsetp  = func->mods;
		return 1;
	}

	if (funcname[0] == 'f' && isdigit (funcname[1]))
	{
		*typep = TERMO_TYPE_FUNCTION;
		*symp  = atoi (funcname + 1);
		return 1;
	}

	// Last-ditch attempt; maybe it's a shift key?
	if (funcname[0] == 's' && funcname2keysym
		(funcname + 1, typep, symp, modmaskp, modsetp))
	{
		*modmaskp |= TERMO_KEYMOD_SHIFT;
		*modsetp  |= TERMO_KEYMOD_SHIFT;
		return 1;
	}

#ifdef DEBUG
	fprintf (stderr, "TODO: Need to convert funcname"
		" %s to a type/sym\n", funcname);
#endif

	return 0;
}

static int
insert_seq (termo_ti_t *ti, const char *seq, trie_node_t *node)
{
	int pos = 0;
	trie_node_t *p = ti->root;

	// Unsigned because we'll be using it as an array subscript
	unsigned char b;

	while ((b = seq[pos]))
	{
		trie_node_t *next = lookup_next (p, b);
		if (!next)
			break;
		p = next;
		pos++;
	}

	while ((b = seq[pos]))
	{
		trie_node_t *next;
		if (seq[pos+1])
			// Intermediate node
			next = new_node_arr (0, 0xff);
		else
			// Final key node
			next = node;

		if (!next)
			return 0;

		switch (p->type)
		{
		case TYPE_ARRAY:
		{
			trie_node_array_t *nar = (trie_node_array_t *) p;
			if (b < nar->min || b > nar->max)
			{
				fprintf (stderr, "fatal: trie insert at 0x%02x is outside of"
					" extent bounds (0x%02x..0x%02x)\n",
					b, nar->min, nar->max);
				abort ();
			}
			nar->arr[b - nar->min] = next;
			p = next;
			break;
		}
		case TYPE_KEY:
		case TYPE_MOUSE:
			fprintf (stderr, "fatal: tried to insert child node in TYPE_KEY\n");
			abort ();
		}

		pos++;
	}
	return 1;
}

termo_driver_t termo_driver_ti =
{
	.name         = "terminfo",
	.new_driver   = new_driver,
	.free_driver  = free_driver,
	.start_driver = start_driver,
	.stop_driver  = stop_driver,
	.peekkey      = peekkey,
};

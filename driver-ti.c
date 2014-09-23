// we want strdup()
#define _XOPEN_SOURCE 600

#include "termkey.h"
#include "termkey-internal.h"

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
#include <sys/types.h>
#include <sys/stat.h>

/* To be efficient at lookups, we store the byte sequence => keyinfo mapping
 * in a trie. This avoids a slow linear search through a flat list of
 * sequences. Because it is likely most nodes will be very sparse, we optimise
 * vector to store an extent map after the database is loaded.
 */

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
	unsigned char min, max; /* INCLUSIVE endpoints of the extent range */
	trie_node_t *arr[]; /* dynamic size at allocation time */
}
trie_node_array_t;

typedef struct
{
	termkey_t *tk;
	trie_node_t *root;

	char *start_string;
	char *stop_string;
}
termkey_ti_t;

static int funcname2keysym (const char *funcname, termkey_type_t *typep,
	termkey_sym_t *symp, int *modmask, int *modsetp);
static int insert_seq (termkey_ti_t *ti, const char *seq, trie_node_t *node);

static trie_node_t *
new_node_key (termkey_type_t type, termkey_sym_t sym, int modmask, int modset)
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
		// FIXME
		fprintf (stderr, "ABORT: lookup_next within a TYPE_KEY node\n");
		abort ();
	case TYPE_ARRAY:
	{
		trie_node_array_t *nar = (trie_node_array_t *) n;
		if (b < nar->min || b > nar->max)
			return NULL;
		return nar->arr[b - nar->min];
	}
	}

	return NULL; // Never reached but keeps compiler happy
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

static int
load_terminfo (termkey_ti_t *ti, const char *term)
{
	int i;

#ifdef HAVE_UNIBILIUM
	unibi_term *unibi = unibi_from_term (term);
	if (!unibi)
		return 0;
#else
	int err;

	/* Have to cast away the const. But it's OK - we know terminfo won't really
	 * modify term */
	if (setupterm ((char *) term, 1, &err) != OK)
		return 0;
#endif

#ifdef HAVE_UNIBILIUM
	for (i = unibi_string_begin_ + 1; i < unibi_string_end_; i++)
#else
	for (i = 0; strfnames[i]; i++)
#endif
	{
		// Only care about the key_* constants
#ifdef HAVE_UNIBILIUM
		const char *name = unibi_name_str (i);
#else
		const char *name = strfnames[i];
#endif
		if (strncmp (name, "key_", 4) != 0)
			continue;

#ifdef HAVE_UNIBILIUM
		const char *value = unibi_get_str (unibi, i);
#else
		const char *value = tigetstr (strnames[i]);
#endif
		if (!value || value == (char*) -1)
			continue;

		struct trie_node *node = NULL;
		if (strcmp (name + 4, "mouse") == 0)
		{
			node = malloc (sizeof *node);
			if (!node)
				return 0;

			node->type = TYPE_MOUSE;
		}
		else
		{
			termkey_type_t type;
			termkey_sym_t sym;
			int mask = 0;
			int set = 0;

			if (!funcname2keysym (name + 4, &type, &sym, &mask, &set))
				continue;

			if (sym == TERMKEY_SYM_NONE)
				continue;

			node = new_node_key (type, sym, mask, set);
		}

		if (node && !insert_seq (ti, value, node))
		{
			free(node);
			return 0;
		}
	}

	/* Take copies of these terminfo strings, in case we build multiple termkey
	 * instances for multiple different termtypes, and it's different by the
	 * time we want to use it
	 */
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

	return 1;
}

static void *
new_driver (termkey_t *tk, const char *term)
{
	termkey_ti_t *ti = malloc (sizeof *ti);
	if (!ti)
		return NULL;

	ti->tk = tk;
	ti->root = new_node_arr (0, 0xff);
	if (!ti->root)
		goto abort_free_ti;

	if (!load_terminfo (ti, term))
		goto abort_free_trie;

	ti->root = compress_trie (ti->root);
	return ti;

abort_free_trie:
	free_trie(ti->root);

abort_free_ti:
	free(ti);

	return NULL;
}

static int
start_driver (termkey_t *tk, void *info)
{
	termkey_ti_t *ti = info;
	struct stat statbuf;
	char *start_string = ti->start_string;
	size_t len;

	if (tk->fd == -1 || !start_string)
		return 1;

	/* The terminfo database will contain keys in application cursor key mode.
	 * We may need to enable that mode
	 */

	// FIXME: isatty() should suffice
	/* There's no point trying to write() to a pipe */
	if (fstat (tk->fd, &statbuf) == -1)
		return 0;

	if (S_ISFIFO (statbuf.st_mode))
		return 1;

	// Can't call putp or tputs because they suck and don't give us fd control
	len = strlen (start_string);
	while (len)
	{
		ssize_t written = write (tk->fd, start_string, len);
		if (written == -1)
			return 0;
		start_string += written;
		len -= written;
	}
	return 1;
}

// XXX: this is the same as above only with a different string
static int
stop_driver (termkey_t *tk, void *info)
{
	termkey_ti_t *ti = info;
	struct stat statbuf;
	char *stop_string = ti->stop_string;
	size_t len;

	if (tk->fd == -1 || !stop_string)
		return 1;

	/* There's no point trying to write() to a pipe */
	if (fstat (tk->fd, &statbuf) == -1)
		return 0;

	if (S_ISFIFO (statbuf.st_mode))
		return 1;

	/* The terminfo database will contain keys in application cursor key mode.
	 * We may need to enable that mode
	 */

	// Can't call putp or tputs because they suck and don't give us fd control
	len = strlen (stop_string);
	while (len)
	{
		ssize_t written = write (tk->fd, stop_string, len);
		if (written == -1)
			return 0;
		stop_string += written;
		len -= written;
	}
	return 1;
}

static void
free_driver (void *info)
{
	termkey_ti_t *ti = info;
	free_trie (ti->root);
	free (ti->start_string);
	free (ti->stop_string);
	free (ti);
}

#define CHARAT(i) (tk->buffer[tk->buffstart + (i)])

static termkey_result_t
peekkey (termkey_t *tk, void *info,
	termkey_key_t *key, int force, size_t *nbytep)
{
	termkey_ti_t *ti = info;

	if (tk->buffcount == 0)
		return tk->is_closed ? TERMKEY_RES_EOF : TERMKEY_RES_NONE;

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
			return TERMKEY_RES_KEY;
		}
		else if (p->type == TYPE_MOUSE)
		{
			tk->buffstart += pos;
			tk->buffcount -= pos;

			termkey_result_t mouse_result =
				(*tk->method.peekkey_mouse) (tk, key, nbytep);

			tk->buffstart -= pos;
			tk->buffcount += pos;

			if (mouse_result == TERMKEY_RES_KEY)
				*nbytep += pos;

			return mouse_result;
		}
	}

	// If p is not NULL then we hadn't walked off the end yet, so we have a
	// partial match
	if (p && !force)
		return TERMKEY_RES_AGAIN;

	return TERMKEY_RES_NONE;
}

static struct func
{
	const char *funcname;
	termkey_type_t type;
	termkey_sym_t sym;
	int mods;
}
funcs[] =
{
	/* THIS LIST MUST REMAIN SORTED! */
	{ "backspace", TERMKEY_TYPE_KEYSYM, TERMKEY_SYM_BACKSPACE, 0 },
	{ "begin",     TERMKEY_TYPE_KEYSYM, TERMKEY_SYM_BEGIN,     0 },
	{ "beg",       TERMKEY_TYPE_KEYSYM, TERMKEY_SYM_BEGIN,     0 },
	{ "btab",      TERMKEY_TYPE_KEYSYM, TERMKEY_SYM_TAB,       TERMKEY_KEYMOD_SHIFT },
	{ "cancel",    TERMKEY_TYPE_KEYSYM, TERMKEY_SYM_CANCEL,    0 },
	{ "clear",     TERMKEY_TYPE_KEYSYM, TERMKEY_SYM_CLEAR,     0 },
	{ "close",     TERMKEY_TYPE_KEYSYM, TERMKEY_SYM_CLOSE,     0 },
	{ "command",   TERMKEY_TYPE_KEYSYM, TERMKEY_SYM_COMMAND,   0 },
	{ "copy",      TERMKEY_TYPE_KEYSYM, TERMKEY_SYM_COPY,      0 },
	{ "dc",        TERMKEY_TYPE_KEYSYM, TERMKEY_SYM_DELETE,    0 },
	{ "down",      TERMKEY_TYPE_KEYSYM, TERMKEY_SYM_DOWN,      0 },
	{ "end",       TERMKEY_TYPE_KEYSYM, TERMKEY_SYM_END,       0 },
	{ "enter",     TERMKEY_TYPE_KEYSYM, TERMKEY_SYM_ENTER,     0 },
	{ "exit",      TERMKEY_TYPE_KEYSYM, TERMKEY_SYM_EXIT,      0 },
	{ "find",      TERMKEY_TYPE_KEYSYM, TERMKEY_SYM_FIND,      0 },
	{ "help",      TERMKEY_TYPE_KEYSYM, TERMKEY_SYM_HELP,      0 },
	{ "home",      TERMKEY_TYPE_KEYSYM, TERMKEY_SYM_HOME,      0 },
	{ "ic",        TERMKEY_TYPE_KEYSYM, TERMKEY_SYM_INSERT,    0 },
	{ "left",      TERMKEY_TYPE_KEYSYM, TERMKEY_SYM_LEFT,      0 },
	{ "mark",      TERMKEY_TYPE_KEYSYM, TERMKEY_SYM_MARK,      0 },
	{ "message",   TERMKEY_TYPE_KEYSYM, TERMKEY_SYM_MESSAGE,   0 },
	{ "mouse",     TERMKEY_TYPE_KEYSYM, TERMKEY_SYM_NONE,      0 },
	{ "move",      TERMKEY_TYPE_KEYSYM, TERMKEY_SYM_MOVE,      0 },
	// Not quite, but it's the best we can do
	{ "next",      TERMKEY_TYPE_KEYSYM, TERMKEY_SYM_PAGEDOWN,  0 },
	{ "npage",     TERMKEY_TYPE_KEYSYM, TERMKEY_SYM_PAGEDOWN,  0 },
	{ "open",      TERMKEY_TYPE_KEYSYM, TERMKEY_SYM_OPEN,      0 },
	{ "options",   TERMKEY_TYPE_KEYSYM, TERMKEY_SYM_OPTIONS,   0 },
	{ "ppage",     TERMKEY_TYPE_KEYSYM, TERMKEY_SYM_PAGEUP,    0 },
	// Not quite, but it's the best we can do
	{ "previous",  TERMKEY_TYPE_KEYSYM, TERMKEY_SYM_PAGEUP,    0 },
	{ "print",     TERMKEY_TYPE_KEYSYM, TERMKEY_SYM_PRINT,     0 },
	{ "redo",      TERMKEY_TYPE_KEYSYM, TERMKEY_SYM_REDO,      0 },
	{ "reference", TERMKEY_TYPE_KEYSYM, TERMKEY_SYM_REFERENCE, 0 },
	{ "refresh",   TERMKEY_TYPE_KEYSYM, TERMKEY_SYM_REFRESH,   0 },
	{ "replace",   TERMKEY_TYPE_KEYSYM, TERMKEY_SYM_REPLACE,   0 },
	{ "restart",   TERMKEY_TYPE_KEYSYM, TERMKEY_SYM_RESTART,   0 },
	{ "resume",    TERMKEY_TYPE_KEYSYM, TERMKEY_SYM_RESUME,    0 },
	{ "right",     TERMKEY_TYPE_KEYSYM, TERMKEY_SYM_RIGHT,     0 },
	{ "save",      TERMKEY_TYPE_KEYSYM, TERMKEY_SYM_SAVE,      0 },
	{ "select",    TERMKEY_TYPE_KEYSYM, TERMKEY_SYM_SELECT,    0 },
	{ "suspend",   TERMKEY_TYPE_KEYSYM, TERMKEY_SYM_SUSPEND,   0 },
	{ "undo",      TERMKEY_TYPE_KEYSYM, TERMKEY_SYM_UNDO,      0 },
	{ "up",        TERMKEY_TYPE_KEYSYM, TERMKEY_SYM_UP,        0 },
	{ NULL },
};

static int
func_compare (const void *key, const void *element)
{
	return strcmp (key, ((struct func *) element)->funcname);
}

static int
funcname2keysym (const char *funcname,
	termkey_type_t *typep, termkey_sym_t *symp, int *modmaskp, int *modsetp)
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
		*typep = TERMKEY_TYPE_FUNCTION;
		// FIXME
		*symp  = atoi (funcname + 1);
		return 1;
	}

	// Last-ditch attempt; maybe it's a shift key?
	if (funcname[0] == 's' && funcname2keysym
		(funcname + 1, typep, symp, modmaskp, modsetp))
	{
		*modmaskp |= TERMKEY_KEYMOD_SHIFT;
		*modsetp  |= TERMKEY_KEYMOD_SHIFT;
		return 1;
	}

#ifdef DEBUG
	fprintf (stderr, "TODO: Need to convert funcname %s to a type/sym\n", funcname);
#endif

	return 0;
}

static int
insert_seq (termkey_ti_t *ti, const char *seq, trie_node_t *node)
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
				// FIXME
				fprintf (stderr, "ASSERT FAIL: Trie insert at 0x%02x is outside of extent bounds (0x%02x..0x%02x)\n",
					b, nar->min, nar->max);
				abort ();
			}
			nar->arr[b - nar->min] = next;
			p = next;
			break;
		}
		case TYPE_KEY:
		case TYPE_MOUSE:
			// FIXME
			fprintf (stderr, "ASSERT FAIL: Tried to insert child node in TYPE_KEY\n");
			abort ();
		}

		pos++;
	}
	return 1;
}

termkey_driver_t termkey_driver_ti =
{
	.name         = "terminfo",

	.new_driver   = new_driver,
	.free_driver  = free_driver,

	.start_driver = start_driver,
	.stop_driver  = stop_driver,

	.peekkey      = peekkey,
};

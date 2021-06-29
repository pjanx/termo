#include "termo.h"
#include "termo-internal.h"

#include <ctype.h>
#include <errno.h>
#include <poll.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <langinfo.h>

#include <stdio.h>

void
termo_check_version (int major, int minor)
{
	if (major != TERMO_VERSION_MAJOR)
		fprintf (stderr, "libtermo major version mismatch;"
			" %d (wants) != %d (library)\n",
			major, TERMO_VERSION_MAJOR);
	else if (minor > TERMO_VERSION_MINOR)
		fprintf (stderr, "libtermo minor version mismatch;"
			" %d (wants) > %d (library)\n",
			minor, TERMO_VERSION_MINOR);
	else
		return;
	exit (1);
}

static termo_driver_t *drivers[] =
{
	&termo_driver_ti,
	&termo_driver_csi,
	NULL,
};

// Forwards for the "protected" methods
static void emit_codepoint (termo_t *tk, uint32_t codepoint, termo_key_t *key);
static termo_result_t peekkey_simple (termo_t *tk,
	termo_key_t *key, int flags, size_t *nbytes);
static termo_result_t peekkey_mouse (termo_t *tk,
	termo_key_t *key, size_t *nbytes);

static termo_sym_t register_c0 (termo_t *tk, termo_sym_t sym,
	unsigned char ctrl, const char *name);
static termo_sym_t register_c0_full (termo_t *tk, termo_sym_t sym,
	int modifier_set, int modifier_mask, unsigned char ctrl, const char *name);

static struct
{
	termo_sym_t sym;
	const char *name;
}
keynames[] =
{
	{ TERMO_SYM_NONE,      "NONE"      },
	{ TERMO_SYM_BACKSPACE, "Backspace" },
	{ TERMO_SYM_TAB,       "Tab"       },
	{ TERMO_SYM_ENTER,     "Enter"     },
	{ TERMO_SYM_ESCAPE,    "Escape"    },
	{ TERMO_SYM_SPACE,     "Space"     },
	{ TERMO_SYM_DEL,       "DEL"       },
	{ TERMO_SYM_UP,        "Up"        },
	{ TERMO_SYM_DOWN,      "Down"      },
	{ TERMO_SYM_LEFT,      "Left"      },
	{ TERMO_SYM_RIGHT,     "Right"     },
	{ TERMO_SYM_BEGIN,     "Begin"     },
	{ TERMO_SYM_FIND,      "Find"      },
	{ TERMO_SYM_INSERT,    "Insert"    },
	{ TERMO_SYM_DELETE,    "Delete"    },
	{ TERMO_SYM_SELECT,    "Select"    },
	{ TERMO_SYM_PAGEUP,    "PageUp"    },
	{ TERMO_SYM_PAGEDOWN,  "PageDown"  },
	{ TERMO_SYM_HOME,      "Home"      },
	{ TERMO_SYM_END,       "End"       },
	{ TERMO_SYM_CANCEL,    "Cancel"    },
	{ TERMO_SYM_CLEAR,     "Clear"     },
	{ TERMO_SYM_CLOSE,     "Close"     },
	{ TERMO_SYM_COMMAND,   "Command"   },
	{ TERMO_SYM_COPY,      "Copy"      },
	{ TERMO_SYM_EXIT,      "Exit"      },
	{ TERMO_SYM_HELP,      "Help"      },
	{ TERMO_SYM_MARK,      "Mark"      },
	{ TERMO_SYM_MESSAGE,   "Message"   },
	{ TERMO_SYM_MOVE,      "Move"      },
	{ TERMO_SYM_OPEN,      "Open"      },
	{ TERMO_SYM_OPTIONS,   "Options"   },
	{ TERMO_SYM_PRINT,     "Print"     },
	{ TERMO_SYM_REDO,      "Redo"      },
	{ TERMO_SYM_REFERENCE, "Reference" },
	{ TERMO_SYM_REFRESH,   "Refresh"   },
	{ TERMO_SYM_REPLACE,   "Replace"   },
	{ TERMO_SYM_RESTART,   "Restart"   },
	{ TERMO_SYM_RESUME,    "Resume"    },
	{ TERMO_SYM_SAVE,      "Save"      },
	{ TERMO_SYM_SUSPEND,   "Suspend"   },
	{ TERMO_SYM_UNDO,      "Undo"      },
	{ TERMO_SYM_KP0,       "KP0"       },
	{ TERMO_SYM_KP1,       "KP1"       },
	{ TERMO_SYM_KP2,       "KP2"       },
	{ TERMO_SYM_KP3,       "KP3"       },
	{ TERMO_SYM_KP4,       "KP4"       },
	{ TERMO_SYM_KP5,       "KP5"       },
	{ TERMO_SYM_KP6,       "KP6"       },
	{ TERMO_SYM_KP7,       "KP7"       },
	{ TERMO_SYM_KP8,       "KP8"       },
	{ TERMO_SYM_KP9,       "KP9"       },
	{ TERMO_SYM_KPENTER,   "KPEnter"   },
	{ TERMO_SYM_KPPLUS,    "KPPlus"    },
	{ TERMO_SYM_KPMINUS,   "KPMinus"   },
	{ TERMO_SYM_KPMULT,    "KPMult"    },
	{ TERMO_SYM_KPDIV,     "KPDiv"     },
	{ TERMO_SYM_KPCOMMA,   "KPComma"   },
	{ TERMO_SYM_KPPERIOD,  "KPPeriod"  },
	{ TERMO_SYM_KPEQUALS,  "KPEquals"  },
	{ 0,                    NULL       },
};

#define CHARAT(i) (tk->buffer[tk->buffstart + (i)])

#ifdef DEBUG
// Some internal deubgging functions

static void
print_buffer (termo_t *tk)
{
	size_t i;
	for (i = 0; i < tk->buffcount && i < 20; i++)
		fprintf (stderr, "%02x ", CHARAT (i));
	if (tk->buffcount > 20)
		fprintf (stderr, "...");
}

static void
print_key (termo_t *tk, termo_key_t *key)
{
	switch (key->type)
	{
	case TERMO_TYPE_KEY:
		fprintf (stderr, "Unicode codepoint=U+%04lx multibyte='%s'",
			(long) key->code.codepoint, key->multibyte);
		break;
	case TERMO_TYPE_FUNCTION:
		fprintf (stderr, "Function F%d", key->code.number);
		break;
	case TERMO_TYPE_KEYSYM:
		fprintf (stderr, "Keysym sym=%d(%s)",
			key->code.sym, termo_get_keyname (tk, key->code.sym));
		break;
	case TERMO_TYPE_MOUSE:
	{
		termo_mouse_event_t ev;
		int button, line, col;
		termo_interpret_mouse (tk, key, &ev, &button, &line, &col);
		fprintf (stderr, "Mouse ev=%d button=%d pos=(%d,%d)\n",
			ev, button, line, col);
		break;
	}
	case TERMO_TYPE_FOCUS:
		fprintf (stderr, "%s\n", key->code.focused ? "Focused" : "Defocused");
		break;
	case TERMO_TYPE_POSITION:
	{
		int line, col;
		termo_interpret_position (tk, key, &line, &col);
		fprintf (stderr, "Position report pos=(%d,%d)\n", line, col);
		break;
	}
	case TERMO_TYPE_MODEREPORT:
	{
		int initial, mode, value;
		termo_interpret_modereport (tk, key, &initial, &mode, &value);
		fprintf (stderr, "Mode report mode=%s %d val=%d\n",
			initial == '?' ? "DEC" : "ANSI", mode, value);
		break;
	}
	case TERMO_TYPE_UNKNOWN_CSI:
		fprintf (stderr, "unknown CSI\n");
	}

	int m = key->modifiers;
	fprintf (stderr, " mod=%s%s%s+%02x",
		(m & TERMO_KEYMOD_CTRL  ? "C" : ""),
		(m & TERMO_KEYMOD_ALT   ? "A" : ""),
		(m & TERMO_KEYMOD_SHIFT ? "S" : ""),
		m & ~(TERMO_KEYMOD_CTRL | TERMO_KEYMOD_ALT | TERMO_KEYMOD_SHIFT));
}

static const char *
res2str (termo_result_t res)
{
	static char errorbuffer[256];

	switch (res)
	{
	case TERMO_RES_KEY:
		return "TERMO_RES_KEY";
	case TERMO_RES_EOF:
		return "TERMO_RES_EOF";
	case TERMO_RES_AGAIN:
		return "TERMO_RES_AGAIN";
	case TERMO_RES_NONE:
		return "TERMO_RES_NONE";
	case TERMO_RES_ERROR:
		snprintf (errorbuffer, sizeof errorbuffer,
			"TERMO_RES_ERROR(errno=%d)\n", errno);
		return (const char*) errorbuffer;
	}

	return "unknown";
}
#endif

// Similar to snprintf(str, size, "%s", src) except it turns CamelCase into
// space separated values
static int
snprint_cameltospaces (char *str, size_t size, const char *src)
{
	int prev_lower = 0;
	size_t l = 0;
	while (*src && l < size - 1)
	{
		if (isupper (*src) && prev_lower)
		{
			if (str)
				str[l++] = ' ';
			if (l >= size - 1)
				break;
		}
		prev_lower = islower (*src);
		str[l++] = tolower (*src++);
	}
	str[l] = 0;

	// For consistency with snprintf, return the number of bytes that would
	// have been written, excluding '\0'
	for (; *src; src++)
	{
		if (isupper (*src) && prev_lower)
			l++;
		prev_lower = islower (*src);
		l++;
	}
	return l;
}

// Similar to strcmp(str, strcamel, n) except that:
//   it compares CamelCase in strcamel with space separated values in str;
//   it takes char**s and updates them
// n counts bytes of strcamel, not str
static int
strpncmp_camel (const char **strp, const char **strcamelp, size_t n)
{
	const char *str = *strp, *strcamel = *strcamelp;
	int prev_lower = 0;

	for (; (*str || *strcamel) && n; n--)
	{
		char b = tolower (*strcamel);
		if (isupper (*strcamel) && prev_lower)
		{
			if (*str != ' ')
				break;
			str++;
			if (*str != b)
				break;
		}
		else if (*str != b)
			break;

		prev_lower = islower (*strcamel);

		str++;
		strcamel++;
	}

	*strp = str;
	*strcamelp = strcamel;
	return *str - *strcamel;
}

static termo_t *
termo_alloc (void)
{
	termo_t *tk = malloc (sizeof *tk);
	if (!tk)
		return NULL;

	// Default all the object fields but don't allocate anything

	tk->fd         = -1;
	tk->flags      = 0;
	tk->canonflags = 0;

	tk->buffer    = NULL;
	tk->buffstart = 0;
	tk->buffcount = 0;
	tk->buffsize  = 256; // bytes
	tk->hightide  = 0;

	tk->restore_termios_valid = false;

	tk->waittime = 50; // msec

	tk->is_closed  = false;
	tk->is_started = false;

	tk->nkeynames = 64;
	tk->keynames  = NULL;

	for (int i = 0; i < 32; i++)
		tk->c0[i].sym = TERMO_SYM_NONE;

	tk->drivers = NULL;

	tk->method.emit_codepoint = &emit_codepoint;
	tk->method.peekkey_simple = &peekkey_simple;
	tk->method.peekkey_mouse  = &peekkey_mouse;

	tk->mouse_proto = TERMO_MOUSE_PROTO_NONE;
	tk->mouse_tracking = TERMO_MOUSE_TRACKING_CLICK;
	tk->guessed_mouse_proto = TERMO_MOUSE_PROTO_NONE;

	tk->ti_data = NULL;
	tk->ti_method.set_mouse_proto = NULL;
	tk->ti_method.set_mouse_tracking_mode = NULL;
	return tk;
}

static int
termo_init (termo_t *tk, const char *term, const char *encoding)
{
	if (!encoding)
		encoding = nl_langinfo (CODESET);

	// If we don't specify the endianity, iconv() outputs the BOM first
	static const uint16_t endianity = 0x0102;
	const char *utf32 = (*(uint8_t *) &endianity == 0x01)
		? "UTF-32BE" : "UTF-32LE";

	if ((tk->to_utf32_conv = iconv_open (utf32, encoding)) == (iconv_t) -1)
		return 0;
	if ((tk->from_utf32_conv = iconv_open (encoding, utf32)) == (iconv_t) -1)
		goto abort_free_to_utf32;

	tk->buffer = malloc (tk->buffsize);
	if (!tk->buffer)
		goto abort_free_from_utf32;

	tk->keynames = malloc (sizeof tk->keynames[0] * tk->nkeynames);
	if (!tk->keynames)
		goto abort_free_buffer;

	int i;
	for (i = 0; i < tk->nkeynames; i++)
		tk->keynames[i] = NULL;
	for (i = 0; keynames[i].name; i++)
		if (termo_register_keyname (tk,
			keynames[i].sym, keynames[i].name) == -1)
			goto abort_free_keynames;

	register_c0 (tk, TERMO_SYM_TAB,       0x09, NULL);
	register_c0 (tk, TERMO_SYM_ENTER,     0x0d, NULL);
	register_c0 (tk, TERMO_SYM_ESCAPE,    0x1b, NULL);

	termo_driver_node_t **tail = &tk->drivers;
	for (i = 0; drivers[i]; i++)
	{
		void *info = (*drivers[i]->new_driver) (tk, term);
		if (!info)
			continue;

#ifdef DEBUG
		fprintf (stderr, "Loading the %s driver...\n", drivers[i]->name);
#endif

		termo_driver_node_t *thisdrv = malloc (sizeof *thisdrv);
		if (!thisdrv)
			goto abort_free_drivers;

		thisdrv->driver = drivers[i];
		thisdrv->info = info;
		thisdrv->next = NULL;

		*tail = thisdrv;
		tail = &thisdrv->next;

#ifdef DEBUG
		fprintf (stderr, "Loaded %s driver\n", drivers[i]->name);
#endif
	}

	if (!tk->drivers)
	{
		errno = ENOENT;
		goto abort_free_keynames;
	}
	return 1;

abort_free_drivers:
	for (termo_driver_node_t *p = tk->drivers; p; )
	{
		(*p->driver->free_driver) (p->info);
		termo_driver_node_t *next = p->next;
		free (p);
		p = next;
	}

abort_free_keynames:
	free (tk->keynames);
abort_free_buffer:
	free (tk->buffer);
abort_free_from_utf32:
	iconv_close (tk->from_utf32_conv);
abort_free_to_utf32:
	iconv_close (tk->to_utf32_conv);
	return 0;
}

termo_t *
termo_new (int fd, const char *encoding, int flags)
{
	termo_t *tk = termo_alloc ();
	if (!tk)
		return NULL;

	tk->fd = fd;
	termo_set_flags (tk, flags);

	const char *term = getenv ("TERM");
	if (!termo_init (tk, term, encoding))
		free (tk);
	else if (!(flags & TERMO_FLAG_NOSTART) && !termo_start (tk))
		termo_free (tk);
	else
		return tk;
	return NULL;
}

termo_t *
termo_new_abstract (const char *term, const char *encoding, int flags)
{
	termo_t *tk = termo_alloc ();
	if (!tk)
		return NULL;

	tk->fd = -1;
	termo_set_flags (tk, flags);

	if (!termo_init (tk, term, encoding))
		free (tk);
	else if (!(flags & TERMO_FLAG_NOSTART) && !termo_start (tk))
		termo_free (tk);
	else
		return tk;
	return NULL;
}

void
termo_free (termo_t *tk)
{
	free (tk->buffer);   tk->buffer   = NULL;
	free (tk->keynames); tk->keynames = NULL;

	iconv_close (tk->to_utf32_conv);
	tk->to_utf32_conv = (iconv_t) -1;
	iconv_close (tk->from_utf32_conv);
	tk->from_utf32_conv = (iconv_t) -1;

	termo_driver_node_t *p, *next;
	for (p = tk->drivers; p; p = next)
	{
		(*p->driver->free_driver) (p->info);
		next = p->next;
		free (p);
	}
	free (tk);
}

void
termo_destroy (termo_t *tk)
{
	if (tk->is_started)
		termo_stop (tk);

	termo_free (tk);
}

int
termo_start (termo_t *tk)
{
	if (tk->is_started)
		return 1;

	if (tk->fd != -1 && !(tk->flags & TERMO_FLAG_NOTERMIOS))
	{
		struct termios termios;
		if (tcgetattr (tk->fd, &termios) == 0)
		{
			tk->restore_termios = termios;
			tk->restore_termios_valid = true;

			termios.c_iflag &= ~(IXON|INLCR|ICRNL);
			termios.c_lflag &= ~(ICANON|ECHO);
			termios.c_cc[VMIN]  = 1;
			termios.c_cc[VTIME] = 0;

			if (tk->flags & TERMO_FLAG_CTRLC)
				// want no signal keys at all, so just disable ISIG
				termios.c_lflag &= ~ISIG;
			else
			{
				// Disable ^\ == VQUIT and ^D == VSUSP but leave ^C as SIGINT
				termios.c_cc[VQUIT] = _POSIX_VDISABLE;
				termios.c_cc[VSUSP] = _POSIX_VDISABLE;
				// Some OSes have ^Y == VDSUSP
#ifdef VDSUSP
				termios.c_cc[VDSUSP] = _POSIX_VDISABLE;
#endif
			}

#ifdef DEBUG
			fprintf (stderr, "Setting termios(3) flags\n");
#endif
			tcsetattr (tk->fd, TCSANOW, &termios);
		}
	}

	termo_driver_node_t *p;
	for (p = tk->drivers; p; p = p->next)
		if (p->driver->start_driver)
			if (!(*p->driver->start_driver) (tk, p->info))
				return 0;

#ifdef DEBUG
	fprintf (stderr, "Drivers started; termo instance %p is ready\n", tk);
#endif

	tk->is_started = 1;
	return 1;
}

int
termo_stop (termo_t *tk)
{
	if (!tk->is_started)
		return 1;

	struct termo_driver_node *p;
	for (p = tk->drivers; p; p = p->next)
		if (p->driver->stop_driver)
			(*p->driver->stop_driver) (tk, p->info);

	if (tk->restore_termios_valid)
		tcsetattr (tk->fd, TCSANOW, &tk->restore_termios);

	tk->is_started = false;
	return 1;
}

int
termo_is_started (termo_t *tk)
{
	return tk->is_started;
}

int
termo_get_fd (termo_t *tk)
{
	return tk->fd;
}

int
termo_get_flags (termo_t *tk)
{
	return tk->flags;
}

void
termo_set_flags (termo_t *tk, int newflags)
{
	tk->flags = newflags;
	if (tk->flags & TERMO_FLAG_SPACESYMBOL)
		tk->canonflags |= TERMO_CANON_SPACESYMBOL;
	else
		tk->canonflags &= ~TERMO_CANON_SPACESYMBOL;
}

void
termo_set_waittime (termo_t *tk, int msec)
{
	tk->waittime = msec;
}

int
termo_get_waittime (termo_t *tk)
{
	return tk->waittime;
}

int
termo_get_canonflags (termo_t *tk)
{
	return tk->canonflags;
}

void
termo_set_canonflags (termo_t *tk, int flags)
{
	tk->canonflags = flags;
	if (tk->canonflags & TERMO_CANON_SPACESYMBOL)
		tk->flags |= TERMO_FLAG_SPACESYMBOL;
	else
		tk->flags &= ~TERMO_FLAG_SPACESYMBOL;
}

size_t
termo_get_buffer_size (termo_t *tk)
{
	return tk->buffsize;
}

int
termo_set_buffer_size (termo_t *tk, size_t size)
{
	unsigned char *buffer = realloc (tk->buffer, size);
	if (!buffer)
		return 0;

	tk->buffer = buffer;
	tk->buffsize = size;
	return 1;
}

size_t
termo_get_buffer_remaining (termo_t *tk)
{
	// Return the total number of free bytes in the buffer,
	// because that's what is available to the user.
	return tk->buffsize - tk->buffcount;
}

termo_mouse_proto_t
termo_get_mouse_proto (termo_t *tk)
{
	return tk->mouse_proto;
}

termo_mouse_proto_t
termo_guess_mouse_proto (termo_t *tk)
{
	return tk->guessed_mouse_proto;
}

int
termo_set_mouse_proto (termo_t *tk, termo_mouse_proto_t proto)
{
	termo_mouse_proto_t old_proto = tk->mouse_proto;
	tk->mouse_proto = proto;

	// Call the TI driver to apply the change if needed
	if (proto == old_proto
	 || !tk->is_started
	 || !tk->ti_method.set_mouse_proto)
		return true;

	// Unsetting the protocol disables tracking; this is a bit hackish
	if (!tk->ti_method.set_mouse_tracking_mode (tk->ti_data,
		tk->mouse_tracking, proto != TERMO_MOUSE_PROTO_NONE))
		return false;

	return tk->ti_method.set_mouse_proto (tk->ti_data, old_proto, false)
		&& tk->ti_method.set_mouse_proto (tk->ti_data, proto, true);
}

termo_mouse_tracking_t
termo_get_mouse_tracking_mode (termo_t *tk)
{
	return tk->mouse_tracking;
}

int
termo_set_mouse_tracking_mode (termo_t *tk, termo_mouse_tracking_t mode)
{
	termo_mouse_tracking_t old_mode = tk->mouse_tracking;
	tk->mouse_tracking = mode;

	// Call the TI driver to apply the change if needed
	if (mode == old_mode
	 || !tk->is_started
	 || !tk->ti_method.set_mouse_tracking_mode)
		return true;

	return tk->ti_method.set_mouse_tracking_mode (tk->ti_data, old_mode, false)
		&& tk->ti_method.set_mouse_tracking_mode (tk->ti_data, mode, true);
}

static void
eat_bytes (termo_t *tk, size_t count)
{
	if (count >= tk->buffcount)
	{
		tk->buffstart = 0;
		tk->buffcount = 0;
		return;
	}

	tk->buffstart += count;
	tk->buffcount -= count;
}

#define MULTIBYTE_INVALID '?'

static void
fill_multibyte (termo_t *tk, termo_key_t *key)
{
	size_t codepoint_len = sizeof key->code.codepoint;
	char *codepoint_ptr = (char *) &key->code.codepoint;
	size_t multibyte_len = sizeof key->multibyte;
	char *multibyte_ptr = (char *) key->multibyte;

	size_t result = iconv (tk->from_utf32_conv,
		&codepoint_ptr, &codepoint_len, &multibyte_ptr, &multibyte_len);
	size_t output = sizeof key->multibyte - multibyte_len;

	// Something broke
	if (result == (size_t) -1 || output == 0)
	{
		key->multibyte[0] = MULTIBYTE_INVALID;
		key->multibyte[1] = 0;
		return;
	}

	// Append a null character, as it wasn't part of the input
	key->multibyte[output] = 0;
}

static termo_result_t
parse_multibyte (termo_t *tk, const unsigned char *bytes, size_t len,
	uint32_t *cp, size_t *nbytep)
{
	size_t multibyte_len = len;
	char *multibyte_ptr = (char *) bytes;
	size_t codepoint_len = sizeof *cp;
	char *codepoint_ptr = (char *) cp;

	// Fingers crossed...
	errno = 0;
	iconv (tk->to_utf32_conv,
		&multibyte_ptr, &multibyte_len, &codepoint_ptr, &codepoint_len);

	// Only one Unicode character could have been processed at maximum,
	// so let's just set the number of processed bytes to the difference
	*nbytep = len - multibyte_len;

	// Nothing has been converted, let's examine what happened
	if (codepoint_ptr == (char *) cp)
	{
		if (errno == 0)
			// The input was probably a shift sequence
			return TERMO_RES_AGAIN;
		if (errno == EINVAL)
			// Incomplete character or shift sequence
			return TERMO_RES_AGAIN;
		if (errno == EILSEQ)
		{
			// Invalid multibyte sequence in the input, let's try going
			// byte after byte in hope we skip it completely
			*cp = MULTIBYTE_INVALID;
			*nbytep = 1;
			return TERMO_RES_KEY;
		}

		// We can't really get E2BIG so what the fuck is going on here
		abort ();
	}
	return TERMO_RES_KEY;
}

static void
emit_codepoint (termo_t *tk, uint32_t codepoint, termo_key_t *key)
{
	if (codepoint == 0)
	{
		// ASCII NUL = Ctrl-Space as well as Ctrl-@ but let's prefer
		// the former to follow the behaviour of libtermkey
		key->type = TERMO_TYPE_KEYSYM;
		key->code.sym = TERMO_SYM_SPACE;
		key->modifiers = TERMO_KEYMOD_CTRL;
	}
	else if (codepoint < 0x20)
	{
		// C0 range
		key->code.codepoint = 0;
		key->modifiers = 0;

		if (!(tk->flags & TERMO_FLAG_NOINTERPRET)
		 && tk->c0[codepoint].sym != TERMO_SYM_UNKNOWN)
		{
			key->code.sym   = tk->c0[codepoint].sym;
			key->modifiers |= tk->c0[codepoint].modifier_set;
		}

		if (!key->code.sym)
		{
			key->type = TERMO_TYPE_KEY;
			// Generically modified Unicode ought not report the SHIFT state,
			// or else we get into complications trying to report Shift-; vs :
			// and so on...  In order to be able to represent Ctrl-Shift-A as
			// CTRL modified unicode A, we need to call Ctrl-A simply 'a',
			// lowercase
			if (codepoint + 0x40 >= 'A' && codepoint + 0x40 <= 'Z')
				// It's a letter - use lowercase instead
				key->code.codepoint = codepoint + 0x60;
			else
				key->code.codepoint = codepoint + 0x40;
			key->modifiers = TERMO_KEYMOD_CTRL;
		}
		else
			key->type = TERMO_TYPE_KEYSYM;
	}
	else if (codepoint == 0x7f && !(tk->flags & TERMO_FLAG_NOINTERPRET))
	{
		// ASCII DEL
		key->type = TERMO_TYPE_KEYSYM;
		key->code.sym = TERMO_SYM_DEL;
		key->modifiers = 0;
	}
	else
	{
		key->type = TERMO_TYPE_KEY;
		key->code.codepoint = codepoint;
		key->modifiers = 0;
	}

	termo_canonicalise (tk, key);

	if (key->type == TERMO_TYPE_KEY)
		fill_multibyte (tk, key);
}

void
termo_canonicalise (termo_t *tk, termo_key_t *key)
{
	int flags = tk->canonflags;

	if (flags & TERMO_CANON_SPACESYMBOL)
	{
		if (key->type == TERMO_TYPE_KEY && key->code.codepoint == 0x20)
		{
			key->type = TERMO_TYPE_KEYSYM;
			key->code.sym = TERMO_SYM_SPACE;
		}
	}
	else
	{
		if (key->type == TERMO_TYPE_KEYSYM
		 && key->code.sym == TERMO_SYM_SPACE)
		{
			key->type = TERMO_TYPE_KEY;
			key->code.codepoint = 0x20;
			fill_multibyte (tk, key);
		}
	}

	if (flags & TERMO_CANON_DELBS)
		if (key->type == TERMO_TYPE_KEYSYM
		 && key->code.sym == TERMO_SYM_DEL)
			key->code.sym = TERMO_SYM_BACKSPACE;
}

static termo_result_t
peekkey (termo_t *tk, termo_key_t *key, int flags, size_t *nbytep)
{
	int again = 0;

	if (!tk->is_started)
	{
		errno = EINVAL;
		return TERMO_RES_ERROR;
	}

#ifdef DEBUG
	fprintf (stderr, "getkey(force=%d): buffer ", force);
	print_buffer (tk);
	fprintf (stderr, "\n");
#endif

	if (tk->hightide)
	{
		tk->buffstart += tk->hightide;
		tk->buffcount -= tk->hightide;
		tk->hightide = 0;
	}

	termo_result_t ret;
	termo_driver_node_t *p;
	for (p = tk->drivers; p; p = p->next)
	{
		ret = (p->driver->peekkey) (tk, p->info, key, flags, nbytep);

#ifdef DEBUG
		fprintf (stderr, "Driver %s yields %s\n",
			p->driver->name, res2str (ret));
#endif

		switch (ret)
		{
			size_t halfsize;
		case TERMO_RES_KEY:
#ifdef DEBUG
			print_key (tk, key); fprintf (stderr, "\n");
#endif
			// Slide the data down to stop it running away
			halfsize = tk->buffsize / 2;
			if (tk->buffstart > halfsize)
			{
				memcpy (tk->buffer, tk->buffer + halfsize, halfsize);
				tk->buffstart -= halfsize;
			}

			// Fall-through
		case TERMO_RES_EOF:
		case TERMO_RES_ERROR:
			return ret;

		case TERMO_RES_AGAIN:
			if (!(flags & PEEKKEY_FORCE))
				again = 1;
		case TERMO_RES_NONE:
			break;
		}
	}

	if (again)
		return TERMO_RES_AGAIN;

	ret = peekkey_simple (tk, key, flags, nbytep);

#ifdef DEBUG
	fprintf (stderr, "getkey_simple(force=%d) yields %s\n",
		force, res2str (ret));
	if (ret == TERMO_RES_KEY)
	{
		print_key (tk, key);
		fprintf (stderr, "\n");
	}
#endif

	return ret;
}

static termo_result_t
peekkey_simple (termo_t *tk, termo_key_t *key, int flags, size_t *nbytep)
{
	if (tk->buffcount == 0)
		return tk->is_closed ? TERMO_RES_EOF : TERMO_RES_NONE;

	unsigned char b0 = CHARAT (0);
	if (b0 == 0x1b)
	{
		if (flags & PEEKKEY_ALT_PREFIXED)
		{
			// We got back here recursively, which means that no driver has
			// returned TERMO_RES_AGAIN -> just return the Escape.  Otherwise
			// we would interpret an indefinite number of <Esc>s as Alt+Esc.
			(*tk->method.emit_codepoint) (tk, b0, key);
			*nbytep = 1;
			return TERMO_RES_KEY;
		}

		// Escape-prefixed value? Might therefore be Alt+key
		if (tk->buffcount == 1)
		{
			// This might be an <Esc> press, or it may want to be part
			// of a longer sequence
			if (!(flags & PEEKKEY_FORCE))
				return TERMO_RES_AGAIN;

			(*tk->method.emit_codepoint) (tk, b0, key);
			*nbytep = 1;
			return TERMO_RES_KEY;
		}

		// Try another key there
		tk->buffstart++;
		tk->buffcount--;

		// Run the full driver
		termo_result_t metakey_result =
			peekkey (tk, key, flags | PEEKKEY_ALT_PREFIXED, nbytep);

		tk->buffstart--;
		tk->buffcount++;

		switch (metakey_result)
		{
		case TERMO_RES_KEY:
			key->modifiers |= TERMO_KEYMOD_ALT;
			(*nbytep)++;
			break;

		case TERMO_RES_NONE:
		case TERMO_RES_EOF:
		case TERMO_RES_AGAIN:
		case TERMO_RES_ERROR:
			break;
		}

		return metakey_result;
	}
	else if (!(tk->flags & TERMO_FLAG_RAW))
	{
		// XXX: this way DEL is never recognised as backspace, even if it is
		//   specified in the terminfo entry key_backspace.  Just because it
		//   doesn't form an escape sequence.
		uint32_t codepoint = MULTIBYTE_INVALID;
		termo_result_t res = parse_multibyte
			(tk, tk->buffer + tk->buffstart, tk->buffcount, &codepoint, nbytep);

		if (res == TERMO_RES_AGAIN && (flags & PEEKKEY_FORCE))
		{
			// There weren't enough bytes for a complete character but
			// caller demands an answer.  About the best thing we can do here
			// is eat as many bytes as we have, and emit a MULTIBYTE_INVALID.
			// If the remaining bytes arrive later, they'll be invalid too.
			*nbytep = tk->buffcount;
			res = TERMO_RES_KEY;
		}

		key->type = TERMO_TYPE_KEY;
		key->modifiers = 0;
		(*tk->method.emit_codepoint) (tk, codepoint, key);
		return res;
	}
	else
	{
		// Non multibyte case - just report the raw byte
		key->type = TERMO_TYPE_KEY;
		key->code.codepoint = b0;
		key->modifiers = 0;

		key->multibyte[0] = b0;
		key->multibyte[1] = 0;

		*nbytep = 1;
		return TERMO_RES_KEY;
	}
}

// REPLACEMENT CHARACTER
#define UTF8_INVALID 0xFFFD

static size_t
parse_utf8_fast (const unsigned char *bytes, size_t len, uint32_t *cp)
{
	size_t nbytes;
	unsigned char b0 = bytes[0];
	if (b0 < 0x80)
	{
		// Single byte ASCII
		*cp = b0;
		return 1;
	}
	else if (b0 < 0xc0)
	{
		// Starts with a continuation byte - that's not right
		*cp = UTF8_INVALID;
		return 1;
	}
	else if (b0 < 0xe0)
	{
		nbytes = 2;
		*cp = b0 & 0x1f;
	}
	else if (b0 < 0xf0)
	{
		nbytes = 3;
		*cp = b0 & 0x0f;
	}
	else if (b0 < 0xf8)
	{
		nbytes = 4;
		*cp = b0 & 0x07;
	}
	else if (b0 < 0xfc)
	{
		nbytes = 5;
		*cp = b0 & 0x03;
	}
	else if (b0 < 0xfe)
	{
		nbytes = 6;
		*cp = b0 & 0x01;
	}
	else
	{
		*cp = UTF8_INVALID;
		return 1;
	}

	for (size_t b = 1; b < nbytes; b++)
	{
		if (b >= len)
			return 0;

		unsigned char cb = bytes[b];
		if (cb < 0x80 || cb >= 0xc0)
		{
			*cp = UTF8_INVALID;
			return b;
		}
		*cp <<= 6;
		*cp |= cb & 0x3f;
	}
	return nbytes;
}

static termo_result_t
parse_1005_value (const unsigned char **bytes, size_t *len, uint32_t *cp)
{
	size_t nbytes = parse_utf8_fast (*bytes, *len, cp);
	if (nbytes == 0)
		return TERMO_RES_AGAIN;

	// XXX: With the current infrastructure I'm not sure how to properly handle
	//   this.  peekkey() isn't made for skipping invalid inputs.
	if (*cp == UTF8_INVALID)
		*cp = 0x20;

	(*bytes) += nbytes;
	(*len) -= nbytes;
	return TERMO_RES_KEY;
}

static termo_result_t
peekkey_mouse (termo_t *tk, termo_key_t *key, size_t *nbytep)
{
	uint32_t b, x, y;

	if (tk->mouse_proto == TERMO_MOUSE_PROTO_UTF8)
	{
		const unsigned char *buff = tk->buffer + tk->buffstart;
		size_t len = tk->buffcount;

		if (parse_1005_value (&buff, &len, &b) == TERMO_RES_AGAIN
		 || parse_1005_value (&buff, &len, &x) == TERMO_RES_AGAIN
		 || parse_1005_value (&buff, &len, &y) == TERMO_RES_AGAIN)
			return TERMO_RES_AGAIN;

		*nbytep = tk->buffcount - len;
	}
	else
	{
		if (tk->buffcount < 3)
			return TERMO_RES_AGAIN;

		b = CHARAT (0);
		x = CHARAT (1);
		y = CHARAT (2);

		*nbytep = 3;
	}

	key->type = TERMO_TYPE_MOUSE;
	key->code.mouse.info = b - 0x20;
	key->code.mouse.x = x - 0x20 - 1;
	key->code.mouse.y = y - 0x20 - 1;

	key->modifiers = (key->code.mouse.info & 0x1c) >> 2;
	key->code.mouse.info &= ~0x1c;

	return TERMO_RES_KEY;
}

termo_result_t
termo_getkey (termo_t *tk, termo_key_t *key)
{
	size_t nbytes = 0;
	termo_result_t ret = peekkey (tk, key, 0, &nbytes);

	if (ret == TERMO_RES_KEY)
		eat_bytes (tk, nbytes);

	if (ret == TERMO_RES_AGAIN)
		// Call peekkey() again in force mode to obtain whatever it can
		(void) peekkey (tk, key, PEEKKEY_FORCE, &nbytes);
		// Don't eat it yet though

	return ret;
}

termo_result_t
termo_getkey_force (termo_t *tk, termo_key_t *key)
{
	size_t nbytes = 0;
	termo_result_t ret = peekkey (tk, key, PEEKKEY_FORCE, &nbytes);

	if (ret == TERMO_RES_KEY)
		eat_bytes (tk, nbytes);

	return ret;
}

termo_result_t
termo_waitkey (termo_t *tk, termo_key_t *key)
{
	if (tk->fd == -1)
	{
		errno = EBADF;
		return TERMO_RES_ERROR;
	}

	while (1)
	{
		termo_result_t ret = termo_getkey (tk, key);

		switch (ret)
		{
		case TERMO_RES_KEY:
		case TERMO_RES_EOF:
		case TERMO_RES_ERROR:
			return ret;

		case TERMO_RES_NONE:
			ret = termo_advisereadable (tk);
			if (ret == TERMO_RES_ERROR)
				return ret;
			break;

		case TERMO_RES_AGAIN:
		{
			if (tk->is_closed)
				// We're closed now. Never going to get more bytes
				// so just go with what we have
				return termo_getkey_force (tk, key);

			struct pollfd fd;
retry:
			fd.fd = tk->fd;
			fd.events = POLLIN;

			int pollret = poll (&fd, 1, tk->waittime);
			if (pollret == -1)
			{
				if (errno == EINTR && !(tk->flags & TERMO_FLAG_EINTR))
					goto retry;

				return TERMO_RES_ERROR;
			}

			if (fd.revents & (POLLIN | POLLHUP | POLLERR))
				ret = termo_advisereadable (tk);
			else
				ret = TERMO_RES_NONE;

			if (ret == TERMO_RES_ERROR)
				return ret;
			if (ret == TERMO_RES_NONE)
				return termo_getkey_force (tk, key);
		}
		}
	}

	// UNREACHABLE
}

termo_result_t
termo_advisereadable (termo_t *tk)
{
	if (tk->fd == -1)
	{
		errno = EBADF;
		return TERMO_RES_ERROR;
	}

	if (tk->buffstart)
	{
		memmove (tk->buffer, tk->buffer + tk->buffstart, tk->buffcount);
		tk->buffstart = 0;
	}

	// Not expecting it ever to be greater but doesn't hurt to handle that
	if (tk->buffcount >= tk->buffsize)
	{
		errno = ENOMEM;
		return TERMO_RES_ERROR;
	}

	ssize_t len;
retry:
	len = read (tk->fd, tk->buffer + tk->buffcount,
		tk->buffsize - tk->buffcount);

	if (len == -1)
	{
		if (errno == EAGAIN)
			return TERMO_RES_NONE;
		if (errno == EINTR && !(tk->flags & TERMO_FLAG_EINTR))
			goto retry;
		return TERMO_RES_ERROR;
	}
	if (len < 1)
	{
		tk->is_closed = true;
		return TERMO_RES_NONE;
	}
	tk->buffcount += len;
	return TERMO_RES_AGAIN;
}

size_t
termo_push_bytes (termo_t *tk, const char *bytes, size_t len)
{
	if (tk->buffstart)
	{
		memmove (tk->buffer, tk->buffer + tk->buffstart, tk->buffcount);
		tk->buffstart = 0;
	}

	// Not expecting it ever to be greater but doesn't hurt to handle that
	if (tk->buffcount >= tk->buffsize)
	{
		errno = ENOMEM;
		return (size_t)-1;
	}

	if (len > tk->buffsize - tk->buffcount)
		len = tk->buffsize - tk->buffcount;

	// memcpy(), not strncpy() in case of null bytes in input
	memcpy (tk->buffer + tk->buffcount, bytes, len);
	tk->buffcount += len;

	return len;
}

termo_sym_t
termo_register_keyname (termo_t *tk, termo_sym_t sym, const char *name)
{
	if (!sym)
		sym = tk->nkeynames;

	if (sym >= tk->nkeynames)
	{
		const char **new_keynames =
			realloc (tk->keynames, sizeof new_keynames[0] * (sym + 1));
		if (!new_keynames)
			return -1;

		tk->keynames = new_keynames;

		// Fill in the hole
		for (int i = tk->nkeynames; i < sym; i++)
			tk->keynames[i] = NULL;

		tk->nkeynames = sym + 1;
	}

	tk->keynames[sym] = name;
	return sym;
}

const char *
termo_get_keyname (termo_t *tk, termo_sym_t sym)
{
	if (sym == TERMO_SYM_UNKNOWN)
		return "UNKNOWN";
	if (sym < tk->nkeynames)
		return tk->keynames[sym];
	return "UNKNOWN";
}

static const char *
termo_lookup_keyname_format (termo_t *tk,
	const char *str, termo_sym_t *sym, termo_format_t format)
{
	// We store an array, so we can't do better than a linear search. Doesn't
	// matter because user won't be calling this too often

	for (*sym = 0; *sym < tk->nkeynames; (*sym)++)
	{
		const char *thiskey = tk->keynames[*sym];
		if (!thiskey)
			continue;
		size_t len = strlen (thiskey);
		if (format & TERMO_FORMAT_LOWERSPACE)
		{
			const char *thisstr = str;
			if (strpncmp_camel (&thisstr, &thiskey, len) == 0)
					return thisstr;
		}
		else if (!strncmp (str, thiskey, len))
			return (char *) str + len;
	}
	return NULL;
}

const char *
termo_lookup_keyname (termo_t *tk, const char *str, termo_sym_t *sym)
{
	return termo_lookup_keyname_format (tk, str, sym, 0);
}

termo_sym_t
termo_keyname2sym (termo_t *tk, const char *keyname)
{
	termo_sym_t sym;
	const char *endp = termo_lookup_keyname (tk, keyname, &sym);
	if (!endp || endp[0])
		return TERMO_SYM_UNKNOWN;
	return sym;
}

static termo_sym_t
register_c0 (termo_t *tk,
	termo_sym_t sym, unsigned char ctrl, const char *name)
{
	return register_c0_full (tk, sym, 0, 0, ctrl, name);
}

static termo_sym_t
register_c0_full (termo_t *tk, termo_sym_t sym,
	int modifier_set, int modifier_mask, unsigned char ctrl, const char *name)
{
	if (ctrl >= 0x20)
	{
		errno = EINVAL;
		return -1;
	}

	if (name)
		sym = termo_register_keyname (tk, sym, name);

	tk->c0[ctrl].sym = sym;
	tk->c0[ctrl].modifier_set = modifier_set;
	tk->c0[ctrl].modifier_mask = modifier_mask;
	return sym;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static struct modnames
{
	const char *shift, *alt, *ctrl;
}
modnames[] =
{
	{ "S",     "A",    "C"    }, // 0
	{ "Shift", "Alt",  "Ctrl" }, // LONGMOD
	{ "S",     "M",    "C"    }, // ALTISMETA
	{ "Shift", "Meta", "Ctrl" }, // ALTISMETA+LONGMOD
	{ "s",     "a",    "c"    }, // LOWERMOD
	{ "shift", "alt",  "ctrl" }, // LOWERMOD+LONGMOD
	{ "s",     "m",    "c"    }, // LOWERMOD+ALTISMETA
	{ "shift", "meta", "ctrl" }, // LOWERMOD+ALTISMETA+LONGMOD
};

typedef const char *(*strfkey_emit_fn) (termo_t *, termo_key_t *, char *);

static size_t
termo_strfkey_generic (termo_t *tk, char *buffer, size_t len,
	termo_key_t *key, termo_format_t format, strfkey_emit_fn emit)
{
	size_t pos = 0;
	int l = 0;

	struct modnames *mods = &modnames[
		!!(format & TERMO_FORMAT_LONGMOD) +
		!!(format & TERMO_FORMAT_ALTISMETA) * 2 +
		!!(format & TERMO_FORMAT_LOWERMOD) * 4];

	int wrapbracket = (format & TERMO_FORMAT_WRAPBRACKET) &&
		(key->type != TERMO_TYPE_KEY || key->modifiers != 0);

	char sep = (format & TERMO_FORMAT_SPACEMOD) ? ' ' : '-';

	if (format & TERMO_FORMAT_CARETCTRL &&
		key->type == TERMO_TYPE_KEY &&
		key->modifiers == TERMO_KEYMOD_CTRL)
	{
		uint32_t codepoint = key->code.codepoint;

		// Handle some of the special casesfirst
		if (codepoint >= 'a' && codepoint <= 'z')
		{
			l = snprintf (buffer + pos, len - pos,
				wrapbracket ? "<^%c>" : "^%c", (char) codepoint - 0x20);
			if (l <= 0)
				return pos;
			pos += l;
			return pos;
		}
		else if ((codepoint >= '@' && codepoint < 'A') ||
			(codepoint > 'Z' && codepoint <= '_'))
		{
			l = snprintf (buffer + pos, len - pos,
				wrapbracket ? "<^%c>" : "^%c", (char) codepoint);
			if (l <= 0)
				return pos;
			pos += l;
			return pos;
		}
	}

	if (wrapbracket)
	{
		l = snprintf (buffer + pos, len - pos, "<");
		if (l <= 0)
			return pos;
		pos += l;
	}

	if (key->modifiers & TERMO_KEYMOD_ALT)
	{
		l = snprintf (buffer + pos, len - pos, "%s%c", mods->alt, sep);
		if (l <= 0)
			return pos;
		pos += l;
	}
	if (key->modifiers & TERMO_KEYMOD_CTRL)
	{
		l = snprintf (buffer + pos, len - pos, "%s%c", mods->ctrl, sep);
		if (l <= 0)
			return pos;
		pos += l;
	}
	if (key->modifiers & TERMO_KEYMOD_SHIFT)
	{
		l = snprintf (buffer + pos, len - pos, "%s%c", mods->shift, sep);
		if (l <= 0)
			return pos;
		pos += l;
	}

	switch (key->type)
	{
	case TERMO_TYPE_KEY:
	{
		char buf[MB_LEN_MAX + 1];
		l = snprintf (buffer + pos, len - pos, "%s", emit (tk, key, buf));
		break;
	}
	case TERMO_TYPE_KEYSYM:
	{
		const char *name = termo_get_keyname (tk, key->code.sym);
		if (format & TERMO_FORMAT_LOWERSPACE)
			l = snprint_cameltospaces (buffer + pos, len - pos, name);
		else
			l = snprintf (buffer + pos, len - pos, "%s", name);
		break;
	}
	case TERMO_TYPE_FUNCTION:
		l = snprintf (buffer + pos, len - pos, "%c%d",
			(format & TERMO_FORMAT_LOWERSPACE ? 'f' : 'F'), key->code.number);
		break;
	case TERMO_TYPE_MOUSE:
	{
		termo_mouse_event_t ev;
		int button;
		int line, col;
		termo_interpret_mouse (tk, key, &ev, &button, &line, &col);

		static const char *evnames[] =
			{ "Unknown", "Press", "Drag", "Release" };
		l = snprintf (buffer + pos, len - pos,
			"Mouse%s(%d)", evnames[ev], button);
		if (format & TERMO_FORMAT_MOUSE_POS)
		{
			if (l <= 0)
				return pos;
			pos += l;
			l = snprintf (buffer + pos, len - pos, " @ (%u,%u)", col, line);
		}
		break;
	}
	case TERMO_TYPE_FOCUS:
		l = snprintf (buffer + pos, len - pos, "Focus(%d)", key->code.focused);
		break;
	case TERMO_TYPE_POSITION:
		l = snprintf (buffer + pos, len - pos, "Position");
		break;
	case TERMO_TYPE_MODEREPORT:
	{
		int initial, mode, value;
		termo_interpret_modereport (tk, key, &initial, &mode, &value);
		if (initial)
			l = snprintf (buffer + pos, len - pos,
				"Mode(%c%d=%d)", initial, mode, value);
		else
			l = snprintf (buffer + pos, len - pos,
				"Mode(%d=%d)", mode, value);
		break;
	}
	case TERMO_TYPE_UNKNOWN_CSI:
		l = snprintf (buffer + pos, len - pos,
			"CSI %c", key->code.number & 0xff);
		break;
	}

	if (l <= 0)
		return pos;
	pos += l;

	if (wrapbracket)
	{
		l = snprintf (buffer + pos, len - pos, ">");
		if (l <= 0)
			return pos;
		pos += l;
	}
	return pos;
}

static const char *
strfkey_emit_locale (termo_t *tk, termo_key_t *key, char buf[])
{
	(void) buf;
	if (!key->multibyte[0]) // In case of user-supplied key structures
		fill_multibyte (tk, key);
	return key->multibyte;
}

size_t
termo_strfkey (termo_t *tk, char *buffer, size_t len,
	termo_key_t *key, termo_format_t format)
{
	return termo_strfkey_generic
		(tk, buffer, len, key, format, strfkey_emit_locale);
}

static inline size_t
utf8_seqlen (uint32_t codepoint)
{
	if (codepoint < 0x0000080) return 1;
	if (codepoint < 0x0000800) return 2;
	if (codepoint < 0x0010000) return 3;
	if (codepoint < 0x0200000) return 4;
	if (codepoint < 0x4000000) return 5;
	return 6;
}

static const char *
strfkey_emit_utf8 (termo_t *tk, termo_key_t *key, char buf[])
{
	(void) tk;
	uint32_t codepoint = key->code.codepoint;
	int nbytes = utf8_seqlen (codepoint);
	buf[nbytes] = 0;

	// This is easier done backwards
	for (int b = nbytes; b-- > 1; codepoint >>= 6)
		buf[b] = 0x80 | (codepoint & 0x3f);

	switch (nbytes)
	{
	case 1: buf[0] =        (codepoint & 0x7f); break;
	case 2: buf[0] = 0xc0 | (codepoint & 0x1f); break;
	case 3: buf[0] = 0xe0 | (codepoint & 0x0f); break;
	case 4: buf[0] = 0xf0 | (codepoint & 0x07); break;
	case 5: buf[0] = 0xf8 | (codepoint & 0x03); break;
	case 6: buf[0] = 0xfc | (codepoint & 0x01); break;
	}
	return buf;
}

size_t
termo_strfkey_utf8 (termo_t *tk, char *buffer, size_t len,
	termo_key_t *key, termo_format_t format)
{
	return termo_strfkey_generic
		(tk, buffer, len, key, format, strfkey_emit_utf8);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

typedef termo_result_t (*strpkey_parse_fn)
	(termo_t *, const unsigned char *, size_t, uint32_t *, size_t *);

static const char *
termo_strpkey_generic (termo_t *tk, const char *str, termo_key_t *key,
	termo_format_t format, strpkey_parse_fn parse)
{
	struct modnames *mods = &modnames[
		!!(format & TERMO_FORMAT_LONGMOD) +
		!!(format & TERMO_FORMAT_ALTISMETA) * 2 +
		!!(format & TERMO_FORMAT_LOWERMOD) * 4];

	key->modifiers = 0;

	if ((format & TERMO_FORMAT_CARETCTRL) && str[0] == '^' && str[1])
	{
		str = termo_strpkey_generic (tk,
			str + 1, key, format & ~TERMO_FORMAT_CARETCTRL, parse);

		if (!str
		 || key->type != TERMO_TYPE_KEY
		 || key->code.codepoint < '@'
		 || key->code.codepoint > '_'
		 || key->modifiers != 0)
			return NULL;

		if (key->code.codepoint >= 'A'
		 && key->code.codepoint <= 'Z')
			key->code.codepoint += 0x20;
		key->modifiers = TERMO_KEYMOD_CTRL;
		fill_multibyte (tk, key);
		return (char *) str;
	}

	const char *sep_at;
	while ((sep_at = strchr (str,
		(format & TERMO_FORMAT_SPACEMOD) ? ' ' : '-')))
	{
		size_t n = sep_at - str;
		if (n == strlen (mods->alt) && !strncmp (mods->alt, str, n))
			key->modifiers |= TERMO_KEYMOD_ALT;
		else if (n == strlen (mods->ctrl) && !strncmp (mods->ctrl, str, n))
			key->modifiers |= TERMO_KEYMOD_CTRL;
		else if (n == strlen (mods->shift) && !strncmp (mods->shift, str, n))
			key->modifiers |= TERMO_KEYMOD_SHIFT;
		else
			break;

		str = sep_at + 1;
	}

	size_t nbytes;
	ssize_t snbytes;
	const char *endstr;

	if ((endstr = termo_lookup_keyname_format
		(tk, str, &key->code.sym, format)))
	{
		key->type = TERMO_TYPE_KEYSYM;
		str = endstr;
	}
	else if (sscanf (str, "F%d%zn", &key->code.number, &snbytes) == 1)
	{
		key->type = TERMO_TYPE_FUNCTION;
		str += snbytes;
	}
	// Multibyte must be last
	else if (parse (tk, (unsigned const char *) str, strlen (str),
		&key->code.codepoint, &nbytes) == TERMO_RES_KEY)
	{
		key->type = TERMO_TYPE_KEY;
		fill_multibyte (tk, key);
		str += nbytes;
	}
	// TODO: Consider mouse events?
	else
		return NULL;

	termo_canonicalise (tk, key);
	return (char *) str;
}

const char *
termo_strpkey (termo_t *tk,
	const char *str, termo_key_t *key, termo_format_t format)
{
	return termo_strpkey_generic (tk, str, key, format, parse_multibyte);
}

static termo_result_t
parse_utf8 (termo_t *tk, const unsigned char *bytes, size_t len,
	uint32_t *cp, size_t *nbytep)
{
	(void) tk;
	size_t nbytes = parse_utf8_fast (bytes, len, cp);
	if (nbytes == 0)
		return TERMO_RES_AGAIN;

	// Check for overlong sequences
	if (nbytes > utf8_seqlen (*cp))
		*cp = UTF8_INVALID;

	// Check for UTF-16 surrogates or invalid *cps
	if ((*cp >= 0xD800 && *cp <= 0xDFFF)
	 || *cp == 0xFFFE
	 || *cp == 0xFFFF)
		*cp = UTF8_INVALID;

	*nbytep = nbytes;
	return TERMO_RES_KEY;
}

const char *
termo_strpkey_utf8 (termo_t *tk,
	const char *str, termo_key_t *key, termo_format_t format)
{
	return termo_strpkey_generic (tk, str, key, format, parse_utf8);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

int
termo_keycmp (termo_t *tk,
	const termo_key_t *key1p, const termo_key_t *key2p)
{
	// Copy the key structs since we'll be modifying them
	termo_key_t key1 = *key1p, key2 = *key2p;

	termo_canonicalise (tk, &key1);
	termo_canonicalise (tk, &key2);

	if (key1.type != key2.type)
		return key1.type - key2.type;

	switch (key1.type)
	{
	case TERMO_TYPE_KEY:
		if (key1.code.codepoint != key2.code.codepoint)
			return key1.code.codepoint - key2.code.codepoint;
		break;
	case TERMO_TYPE_KEYSYM:
		if (key1.code.sym != key2.code.sym)
			return key1.code.sym - key2.code.sym;
		break;
	case TERMO_TYPE_FUNCTION:
	case TERMO_TYPE_UNKNOWN_CSI:
		if (key1.code.number != key2.code.number)
			return key1.code.number - key2.code.number;
		break;
	case TERMO_TYPE_MOUSE:
	{
		int cmp = memcmp (&key1.code.mouse, &key2.code.mouse,
			sizeof key1.code.mouse);
		if (cmp != 0)
			return cmp;
		break;
	}
	case TERMO_TYPE_FOCUS:
		return key1.code.focused - key2.code.focused;
	case TERMO_TYPE_POSITION:
	{
		int line1, col1, line2, col2;
		termo_interpret_position (tk, &key1, &line1, &col1);
		termo_interpret_position (tk, &key2, &line2, &col2);
		if (line1 != line2)
			return line1 - line2;
		return col1 - col2;
	}
	case TERMO_TYPE_MODEREPORT:
	{
		int initial1, initial2, mode1, mode2, value1, value2;
		termo_interpret_modereport (tk, &key1, &initial1, &mode1, &value1);
		termo_interpret_modereport (tk, &key2, &initial2, &mode2, &value2);
		if (initial1 != initial2)
			return initial1 - initial2;
		if (mode1 != mode2)
			return mode1 - mode2;
		return value1 - value2;
	}
	}
	return key1.modifiers - key2.modifiers;
}


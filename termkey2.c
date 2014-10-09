#include "termkey2.h"
#include "termkey2-internal.h"

#include <ctype.h>
#include <errno.h>
#include <poll.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <langinfo.h>

#include <stdio.h>

void
termkey_check_version (int major, int minor)
{
	if (major != TERMKEY_VERSION_MAJOR)
		fprintf (stderr, "libtermkey major version mismatch;"
			" %d (wants) != %d (library)\n",
			major, TERMKEY_VERSION_MAJOR);
	else if (minor > TERMKEY_VERSION_MINOR)
		fprintf (stderr, "libtermkey minor version mismatch;"
			" %d (wants) > %d (library)\n",
			minor, TERMKEY_VERSION_MINOR);
	else
		return;
	exit (1);
}

static termkey_driver_t *drivers[] =
{
	&termkey_driver_ti,
	&termkey_driver_csi,
	NULL,
};

// Forwards for the "protected" methods
static void emit_codepoint (termkey_t *tk, uint32_t codepoint, termkey_key_t *key);
static termkey_result_t peekkey_simple (termkey_t *tk,
	termkey_key_t *key, int force, size_t *nbytes);
static termkey_result_t peekkey_mouse (termkey_t *tk,
	termkey_key_t *key, size_t *nbytes);

static termkey_sym_t register_c0 (termkey_t *tk, termkey_sym_t sym,
	unsigned char ctrl, const char *name);
static termkey_sym_t register_c0_full (termkey_t *tk, termkey_sym_t sym,
	int modifier_set, int modifier_mask, unsigned char ctrl, const char *name);

static struct
{
	termkey_sym_t sym;
	const char *name;
}
keynames[] =
{
	{ TERMKEY_SYM_NONE,      "NONE"      },
	{ TERMKEY_SYM_BACKSPACE, "Backspace" },
	{ TERMKEY_SYM_TAB,       "Tab"       },
	{ TERMKEY_SYM_ENTER,     "Enter"     },
	{ TERMKEY_SYM_ESCAPE,    "Escape"    },
	{ TERMKEY_SYM_SPACE,     "Space"     },
	{ TERMKEY_SYM_DEL,       "DEL"       },
	{ TERMKEY_SYM_UP,        "Up"        },
	{ TERMKEY_SYM_DOWN,      "Down"      },
	{ TERMKEY_SYM_LEFT,      "Left"      },
	{ TERMKEY_SYM_RIGHT,     "Right"     },
	{ TERMKEY_SYM_BEGIN,     "Begin"     },
	{ TERMKEY_SYM_FIND,      "Find"      },
	{ TERMKEY_SYM_INSERT,    "Insert"    },
	{ TERMKEY_SYM_DELETE,    "Delete"    },
	{ TERMKEY_SYM_SELECT,    "Select"    },
	{ TERMKEY_SYM_PAGEUP,    "PageUp"    },
	{ TERMKEY_SYM_PAGEDOWN,  "PageDown"  },
	{ TERMKEY_SYM_HOME,      "Home"      },
	{ TERMKEY_SYM_END,       "End"       },
	{ TERMKEY_SYM_CANCEL,    "Cancel"    },
	{ TERMKEY_SYM_CLEAR,     "Clear"     },
	{ TERMKEY_SYM_CLOSE,     "Close"     },
	{ TERMKEY_SYM_COMMAND,   "Command"   },
	{ TERMKEY_SYM_COPY,      "Copy"      },
	{ TERMKEY_SYM_EXIT,      "Exit"      },
	{ TERMKEY_SYM_HELP,      "Help"      },
	{ TERMKEY_SYM_MARK,      "Mark"      },
	{ TERMKEY_SYM_MESSAGE,   "Message"   },
	{ TERMKEY_SYM_MOVE,      "Move"      },
	{ TERMKEY_SYM_OPEN,      "Open"      },
	{ TERMKEY_SYM_OPTIONS,   "Options"   },
	{ TERMKEY_SYM_PRINT,     "Print"     },
	{ TERMKEY_SYM_REDO,      "Redo"      },
	{ TERMKEY_SYM_REFERENCE, "Reference" },
	{ TERMKEY_SYM_REFRESH,   "Refresh"   },
	{ TERMKEY_SYM_REPLACE,   "Replace"   },
	{ TERMKEY_SYM_RESTART,   "Restart"   },
	{ TERMKEY_SYM_RESUME,    "Resume"    },
	{ TERMKEY_SYM_SAVE,      "Save"      },
	{ TERMKEY_SYM_SUSPEND,   "Suspend"   },
	{ TERMKEY_SYM_UNDO,      "Undo"      },
	{ TERMKEY_SYM_KP0,       "KP0"       },
	{ TERMKEY_SYM_KP1,       "KP1"       },
	{ TERMKEY_SYM_KP2,       "KP2"       },
	{ TERMKEY_SYM_KP3,       "KP3"       },
	{ TERMKEY_SYM_KP4,       "KP4"       },
	{ TERMKEY_SYM_KP5,       "KP5"       },
	{ TERMKEY_SYM_KP6,       "KP6"       },
	{ TERMKEY_SYM_KP7,       "KP7"       },
	{ TERMKEY_SYM_KP8,       "KP8"       },
	{ TERMKEY_SYM_KP9,       "KP9"       },
	{ TERMKEY_SYM_KPENTER,   "KPEnter"   },
	{ TERMKEY_SYM_KPPLUS,    "KPPlus"    },
	{ TERMKEY_SYM_KPMINUS,   "KPMinus"   },
	{ TERMKEY_SYM_KPMULT,    "KPMult"    },
	{ TERMKEY_SYM_KPDIV,     "KPDiv"     },
	{ TERMKEY_SYM_KPCOMMA,   "KPComma"   },
	{ TERMKEY_SYM_KPPERIOD,  "KPPeriod"  },
	{ TERMKEY_SYM_KPEQUALS,  "KPEquals"  },
	{ 0,                      NULL       },
};

#define CHARAT(i) (tk->buffer[tk->buffstart + (i)])

#ifdef DEBUG
/* Some internal deubgging functions */

static void
print_buffer (termkey_t *tk)
{
	size_t i;
	for (i = 0; i < tk->buffcount && i < 20; i++)
		fprintf (stderr, "%02x ", CHARAT (i));
	if (tk->buffcount > 20)
		fprintf (stderr, "...");
}

static void
print_key (termkey_t *tk, termkey_key_t *key)
{
	switch (key->type)
	{
	case TERMKEY_TYPE_KEY:
		fprintf (stderr, "Unicode codepoint=U+%04lx multibyte='%s'",
			(long) key->code.codepoint, key->multibyte);
		break;
	case TERMKEY_TYPE_FUNCTION:
		fprintf (stderr, "Function F%d", key->code.number);
		break;
	case TERMKEY_TYPE_KEYSYM:
		fprintf (stderr, "Keysym sym=%d(%s)",
			key->code.sym, termkey_get_keyname (tk, key->code.sym));
		break;
	case TERMKEY_TYPE_MOUSE:
	{
		termkey_mouse_event_t ev;
		int button, line, col;
		termkey_interpret_mouse (tk, key, &ev, &button, &line, &col);
		fprintf (stderr, "Mouse ev=%d button=%d pos=(%d,%d)\n",
			ev, button, line, col);
		break;
	}
	case TERMKEY_TYPE_POSITION:
	{
		int line, col;
		termkey_interpret_position (tk, key, &line, &col);
		fprintf (stderr, "Position report pos=(%d,%d)\n", line, col);
		break;
	}
	case TERMKEY_TYPE_MODEREPORT:
	{
		int initial, mode, value;
		termkey_interpret_modereport (tk, key, &initial, &mode, &value);
		fprintf (stderr, "Mode report mode=%s %d val=%d\n",
			initial == '?' ? "DEC" : "ANSI", mode, value);
		break;
	}
	case TERMKEY_TYPE_UNKNOWN_CSI:
		fprintf (stderr, "unknown CSI\n");
	}

	int m = key->modifiers;
	fprintf (stderr, " mod=%s%s%s+%02x",
		(m & TERMKEY_KEYMOD_CTRL  ? "C" : ""),
		(m & TERMKEY_KEYMOD_ALT   ? "A" : ""),
		(m & TERMKEY_KEYMOD_SHIFT ? "S" : ""),
		m & ~(TERMKEY_KEYMOD_CTRL | TERMKEY_KEYMOD_ALT | TERMKEY_KEYMOD_SHIFT));
}

static const char *
res2str (termkey_result_t res)
{
	static char errorbuffer[256];

	switch (res)
	{
	case TERMKEY_RES_KEY:
		return "TERMKEY_RES_KEY";
	case TERMKEY_RES_EOF:
		return "TERMKEY_RES_EOF";
	case TERMKEY_RES_AGAIN:
		return "TERMKEY_RES_AGAIN";
	case TERMKEY_RES_NONE:
		return "TERMKEY_RES_NONE";
	case TERMKEY_RES_ERROR:
		snprintf (errorbuffer, sizeof errorbuffer,
			"TERMKEY_RES_ERROR(errno=%d)\n", errno);
		return (const char*) errorbuffer;
	}

	return "unknown";
}
#endif

/* Similar to snprintf(str, size, "%s", src) except it turns CamelCase into
 * space separated values
 */
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

	/* For consistency with snprintf, return the number of bytes that would have
	 * been written, excluding '\0' */
	for (; *src; src++)
	{
		if (isupper (*src) && prev_lower)
			l++;
		prev_lower = islower (*src);
		l++;
	}
	return l;
}

/* Similar to strcmp(str, strcamel, n) except that:
 *    it compares CamelCase in strcamel with space separated values in str;
 *    it takes char**s and updates them
 * n counts bytes of strcamel, not str
 */
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

static termkey_t *
termkey_alloc (void)
{
	termkey_t *tk = malloc (sizeof *tk);
	if (!tk)
		return NULL;

	/* Default all the object fields but don't allocate anything */

	tk->fd         = -1;
	tk->flags      = 0;
	tk->canonflags = 0;

	tk->buffer    = NULL;
	tk->buffstart = 0;
	tk->buffcount = 0;
	tk->buffsize  = 256; /* bytes */
	tk->hightide  = 0;

	tk->restore_termios_valid = false;

	tk->waittime = 50; /* msec */

	tk->is_closed  = false;
	tk->is_started = false;

	tk->nkeynames = 64;
	tk->keynames  = NULL;

	for (int i = 0; i < 32; i++)
		tk->c0[i].sym = TERMKEY_SYM_NONE;

	tk->drivers = NULL;

	tk->method.emit_codepoint = &emit_codepoint;
	tk->method.peekkey_simple = &peekkey_simple;
	tk->method.peekkey_mouse  = &peekkey_mouse;
	return tk;
}

static int
termkey_init (termkey_t *tk, const char *term, const char *encoding)
{
	if (!encoding)
		encoding = nl_langinfo (CODESET);

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
		if (termkey_register_keyname (tk,
			keynames[i].sym, keynames[i].name) == -1)
			goto abort_free_keynames;

	register_c0 (tk, TERMKEY_SYM_BACKSPACE, 0x08, NULL);
	register_c0 (tk, TERMKEY_SYM_TAB,       0x09, NULL);
	register_c0 (tk, TERMKEY_SYM_ENTER,     0x0d, NULL);
	register_c0 (tk, TERMKEY_SYM_ESCAPE,    0x1b, NULL);

	termkey_driver_node_t **tail = &tk->drivers;
	for (i = 0; drivers[i]; i++)
	{
		void *info = (*drivers[i]->new_driver) (tk, term);
		if (!info)
			continue;

#ifdef DEBUG
		fprintf (stderr, "Loading the %s driver...\n", drivers[i]->name);
#endif

		termkey_driver_node_t *thisdrv = malloc (sizeof *thisdrv);
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
	for (termkey_driver_node_t *p = tk->drivers; p; )
	{
		(*p->driver->free_driver) (p->info);
		termkey_driver_node_t *next = p->next;
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

termkey_t *
termkey_new (int fd, const char *encoding, int flags)
{
	termkey_t *tk = termkey_alloc ();
	if (!tk)
		return NULL;

	tk->fd = fd;
	termkey_set_flags (tk, flags);

	const char *term = getenv ("TERM");
	if (termkey_init (tk, term, encoding)
	 && termkey_start (tk))
		return tk;

	free (tk);
	return NULL;
}

termkey_t *
termkey_new_abstract (const char *term, const char *encoding, int flags)
{
	termkey_t *tk = termkey_alloc ();
	if (!tk)
		return NULL;

	tk->fd = -1;
	termkey_set_flags (tk, flags);

	if (!termkey_init (tk, term, encoding))
	{
		free (tk);
		return NULL;
	}

	termkey_start (tk);
	return tk;
}

void
termkey_free (termkey_t *tk)
{
	free (tk->buffer);   tk->buffer   = NULL;
	free (tk->keynames); tk->keynames = NULL;

	iconv_close (tk->to_utf32_conv);
	tk->to_utf32_conv = (iconv_t) -1;
	iconv_close (tk->from_utf32_conv);
	tk->from_utf32_conv = (iconv_t) -1;

	termkey_driver_node_t *p, *next;
	for (p = tk->drivers; p; p = next)
	{
		(*p->driver->free_driver) (p->info);
		next = p->next;
		free (p);
	}
	free (tk);
}

void
termkey_destroy (termkey_t *tk)
{
	if (tk->is_started)
		termkey_stop (tk);

	termkey_free (tk);
}

int
termkey_start (termkey_t *tk)
{
	if (tk->is_started)
		return 1;

	if (tk->fd != -1 && !(tk->flags & TERMKEY_FLAG_NOTERMIOS))
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

			if (tk->flags & TERMKEY_FLAG_CTRLC)
				/* want no signal keys at all, so just disable ISIG */
				termios.c_lflag &= ~ISIG;
			else
			{
				/* Disable ^\ == VQUIT and ^D == VSUSP but leave ^C as SIGINT */
				termios.c_cc[VQUIT] = _POSIX_VDISABLE;
				termios.c_cc[VSUSP] = _POSIX_VDISABLE;
				/* Some OSes have ^Y == VDSUSP */
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

	termkey_driver_node_t *p;
	for (p = tk->drivers; p; p = p->next)
		if (p->driver->start_driver)
			if (!(*p->driver->start_driver) (tk, p->info))
				return 0;

#ifdef DEBUG
	fprintf (stderr, "Drivers started; termkey instance %p is ready\n", tk);
#endif

	tk->is_started = 1;
	return 1;
}

int
termkey_stop (termkey_t *tk)
{
	if (!tk->is_started)
		return 1;

	struct termkey_driver_node *p;
	for (p = tk->drivers; p; p = p->next)
		if (p->driver->stop_driver)
			(*p->driver->stop_driver) (tk, p->info);

	if (tk->restore_termios_valid)
		tcsetattr (tk->fd, TCSANOW, &tk->restore_termios);

	tk->is_started = false;
	return 1;
}

int
termkey_is_started (termkey_t *tk)
{
	return tk->is_started;
}

int
termkey_get_fd (termkey_t *tk)
{
	return tk->fd;
}

int
termkey_get_flags (termkey_t *tk)
{
	return tk->flags;
}

void
termkey_set_flags (termkey_t *tk, int newflags)
{
	tk->flags = newflags;
	if (tk->flags & TERMKEY_FLAG_SPACESYMBOL)
		tk->canonflags |= TERMKEY_CANON_SPACESYMBOL;
	else
		tk->canonflags &= ~TERMKEY_CANON_SPACESYMBOL;
}

void
termkey_set_waittime (termkey_t *tk, int msec)
{
	tk->waittime = msec;
}

int
termkey_get_waittime (termkey_t *tk)
{
	return tk->waittime;
}

int
termkey_get_canonflags (termkey_t *tk)
{
	return tk->canonflags;
}

void
termkey_set_canonflags (termkey_t *tk, int flags)
{
	tk->canonflags = flags;
	if (tk->canonflags & TERMKEY_CANON_SPACESYMBOL)
		tk->flags |= TERMKEY_FLAG_SPACESYMBOL;
	else
		tk->flags &= ~TERMKEY_FLAG_SPACESYMBOL;
}

size_t
termkey_get_buffer_size (termkey_t *tk)
{
	return tk->buffsize;
}

int
termkey_set_buffer_size (termkey_t *tk, size_t size)
{
	unsigned char *buffer = realloc (tk->buffer, size);
	if (!buffer)
		return 0;

	tk->buffer = buffer;
	tk->buffsize = size;
	return 1;
}

size_t
termkey_get_buffer_remaining (termkey_t *tk)
{
	/* Return the total number of free bytes in the buffer,
	 * because that's what is available to the user. */
	return tk->buffsize - tk->buffcount;
}

static void
eat_bytes (termkey_t *tk, size_t count)
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
fill_multibyte (termkey_t *tk, termkey_key_t *key)
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

	// Append a null character, as it wasn't port of the input
	key->multibyte[output] = 0;
}

static termkey_result_t
parse_multibyte (termkey_t *tk, const unsigned char *bytes, size_t len,
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
			return TERMKEY_RES_AGAIN;
		if (errno == EINVAL)
			// Incomplete character or shift sequence
			return TERMKEY_RES_AGAIN;
		if (errno == EILSEQ)
		{
			// Invalid multibyte sequence in the input, let's try going
			// byte after byte in hope we skip it completely
			*cp = MULTIBYTE_INVALID;
			*nbytep = 1;
			return TERMKEY_RES_KEY;
		}

		// We can't really get E2BIG so what the fuck is going on here
		abort ();
	}
	return TERMKEY_RES_KEY;
}

static void
emit_codepoint (termkey_t *tk, uint32_t codepoint, termkey_key_t *key)
{
	if (codepoint < 0x20)
	{
		// C0 range
		key->code.codepoint = 0;
		key->modifiers = 0;

		if (!(tk->flags & TERMKEY_FLAG_NOINTERPRET)
		 && tk->c0[codepoint].sym != TERMKEY_SYM_UNKNOWN)
		{
			key->code.sym   = tk->c0[codepoint].sym;
			key->modifiers |= tk->c0[codepoint].modifier_set;
		}

		if (!key->code.sym)
		{
			key->type = TERMKEY_TYPE_KEY;
			/* Generically modified Unicode ought not report the SHIFT state,
			 * or else we get into complications trying to report Shift-; vs :
			 * and so on...  In order to be able to represent Ctrl-Shift-A as
			 * CTRL modified unicode A, we need to call Ctrl-A simply 'a',
			 * lowercase
			 */
			if (codepoint + 0x40 >= 'A' && codepoint + 0x40 <= 'Z')
				// It's a letter - use lowercase instead
				key->code.codepoint = codepoint + 0x60;
			else
				key->code.codepoint = codepoint + 0x40;
			key->modifiers = TERMKEY_KEYMOD_CTRL;
		}
		else
			key->type = TERMKEY_TYPE_KEYSYM;
	}
	else if (codepoint == 0x7f && !(tk->flags & TERMKEY_FLAG_NOINTERPRET))
	{
		// ASCII DEL
		key->type = TERMKEY_TYPE_KEYSYM;
		key->code.sym = TERMKEY_SYM_DEL;
		key->modifiers = 0;
	}
	else
	{
		key->type = TERMKEY_TYPE_KEY;
		key->code.codepoint = codepoint;
		key->modifiers = 0;
	}

	termkey_canonicalise (tk, key);

	if (key->type == TERMKEY_TYPE_KEY)
		fill_multibyte (tk, key);
}

void
termkey_canonicalise (termkey_t *tk, termkey_key_t *key)
{
	int flags = tk->canonflags;

	if (flags & TERMKEY_CANON_SPACESYMBOL)
	{
		if (key->type == TERMKEY_TYPE_KEY && key->code.codepoint == 0x20)
		{
			key->type = TERMKEY_TYPE_KEYSYM;
			key->code.sym = TERMKEY_SYM_SPACE;
		}
	}
	else
	{
		if (key->type == TERMKEY_TYPE_KEYSYM
		 && key->code.sym == TERMKEY_SYM_SPACE)
		{
			key->type = TERMKEY_TYPE_KEY;
			key->code.codepoint = 0x20;
			fill_multibyte (tk, key);
		}
	}

	if (flags & TERMKEY_CANON_DELBS)
		if (key->type == TERMKEY_TYPE_KEYSYM
		 && key->code.sym == TERMKEY_SYM_DEL)
			key->code.sym = TERMKEY_SYM_BACKSPACE;
}

static termkey_result_t
peekkey (termkey_t *tk, termkey_key_t *key, int force, size_t *nbytep)
{
	int again = 0;

	if (!tk->is_started)
	{
		errno = EINVAL;
		return TERMKEY_RES_ERROR;
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

	termkey_result_t ret;
	termkey_driver_node_t *p;
	for (p = tk->drivers; p; p = p->next)
	{
		ret = (p->driver->peekkey) (tk, p->info, key, force, nbytep);

#ifdef DEBUG
		fprintf (stderr, "Driver %s yields %s\n",
			p->driver->name, res2str (ret));
#endif

		switch (ret)
		{
		case TERMKEY_RES_KEY:
		{
#ifdef DEBUG
			print_key (tk, key); fprintf (stderr, "\n");
#endif
			// Slide the data down to stop it running away
			size_t halfsize = tk->buffsize / 2;
			if (tk->buffstart > halfsize)
			{
				memcpy (tk->buffer, tk->buffer + halfsize, halfsize);
				tk->buffstart -= halfsize;
			}

			/* fallthrough */
		}
		case TERMKEY_RES_EOF:
		case TERMKEY_RES_ERROR:
			return ret;

		case TERMKEY_RES_AGAIN:
			if (!force)
				again = 1;
		case TERMKEY_RES_NONE:
			break;
		}
	}

	if (again)
		return TERMKEY_RES_AGAIN;

	ret = peekkey_simple (tk, key, force, nbytep);

#ifdef DEBUG
	fprintf (stderr, "getkey_simple(force=%d) yields %s\n",
		force, res2str (ret));
	if (ret == TERMKEY_RES_KEY)
	{
		print_key (tk, key);
		fprintf (stderr, "\n");
	}
#endif

	return ret;
}

static termkey_result_t
peekkey_simple (termkey_t *tk, termkey_key_t *key, int force, size_t *nbytep)
{
	if (tk->buffcount == 0)
		return tk->is_closed ? TERMKEY_RES_EOF : TERMKEY_RES_NONE;

	unsigned char b0 = CHARAT (0);
	if (b0 == 0x1b)
	{
		// Escape-prefixed value? Might therefore be Alt+key
		if (tk->buffcount == 1)
		{
			// This might be an <Esc> press, or it may want to be part
			// of a longer sequence
			if (!force)
				return TERMKEY_RES_AGAIN;

			(*tk->method.emit_codepoint) (tk, b0, key);
			*nbytep = 1;
			return TERMKEY_RES_KEY;
		}

		// Try another key there
		tk->buffstart++;
		tk->buffcount--;

		// Run the full driver
		termkey_result_t metakey_result = peekkey (tk, key, force, nbytep);

		tk->buffstart--;
		tk->buffcount++;

		switch (metakey_result)
		{
		case TERMKEY_RES_KEY:
			key->modifiers |= TERMKEY_KEYMOD_ALT;
			(*nbytep)++;
			break;

		case TERMKEY_RES_NONE:
		case TERMKEY_RES_EOF:
		case TERMKEY_RES_AGAIN:
		case TERMKEY_RES_ERROR:
			break;
		}

		return metakey_result;
	}
	else if (!(tk->flags & TERMKEY_FLAG_RAW))
	{
		uint32_t codepoint;
		termkey_result_t res = parse_multibyte
			(tk, tk->buffer + tk->buffstart, tk->buffcount, &codepoint, nbytep);

		if (res == TERMKEY_RES_AGAIN && force)
		{
			/* There weren't enough bytes for a complete character but
			 * caller demands an answer.  About the best thing we can do here
			 * is eat as many bytes as we have, and emit a MULTIBYTE_INVALID.
			 * If the remaining bytes arrive later, they'll be invalid too.
			 */
			codepoint = MULTIBYTE_INVALID;
			*nbytep = tk->buffcount;
			res = TERMKEY_RES_KEY;
		}

		key->type = TERMKEY_TYPE_KEY;
		key->modifiers = 0;
		(*tk->method.emit_codepoint) (tk, codepoint, key);
		return res;
	}
	else
	{
		// Non multibyte case - just report the raw byte
		key->type = TERMKEY_TYPE_KEY;
		key->code.codepoint = b0;
		key->modifiers = 0;

		key->multibyte[0] = b0;
		key->multibyte[1] = 0;

		*nbytep = 1;
		return TERMKEY_RES_KEY;
	}
}

static termkey_result_t
peekkey_mouse (termkey_t *tk, termkey_key_t *key, size_t *nbytep)
{
	if (tk->buffcount < 3)
		return TERMKEY_RES_AGAIN;

	key->type = TERMKEY_TYPE_MOUSE;
	key->code.mouse[0] = CHARAT (0) - 0x20;
	key->code.mouse[1] = CHARAT (1) - 0x20;
	key->code.mouse[2] = CHARAT (2) - 0x20;
	key->code.mouse[3] = 0;

	key->modifiers = (key->code.mouse[0] & 0x1c) >> 2;
	key->code.mouse[0] &= ~0x1c;

	*nbytep = 3;
	return TERMKEY_RES_KEY;
}

termkey_result_t
termkey_getkey (termkey_t *tk, termkey_key_t *key)
{
	size_t nbytes = 0;
	termkey_result_t ret = peekkey (tk, key, 0, &nbytes);

	if (ret == TERMKEY_RES_KEY)
		eat_bytes (tk, nbytes);

	if (ret == TERMKEY_RES_AGAIN)
		/* Call peekkey() again in force mode to obtain whatever it can */
		(void) peekkey (tk, key, 1, &nbytes);
		/* Don't eat it yet though */

	return ret;
}

termkey_result_t
termkey_getkey_force (termkey_t *tk, termkey_key_t *key)
{
	size_t nbytes = 0;
	termkey_result_t ret = peekkey (tk, key, 1, &nbytes);

	if (ret == TERMKEY_RES_KEY)
		eat_bytes (tk, nbytes);

	return ret;
}

termkey_result_t
termkey_waitkey (termkey_t *tk, termkey_key_t *key)
{
	if (tk->fd == -1)
	{
		errno = EBADF;
		return TERMKEY_RES_ERROR;
	}

	while (1)
	{
		termkey_result_t ret = termkey_getkey (tk, key);

		switch (ret)
		{
		case TERMKEY_RES_KEY:
		case TERMKEY_RES_EOF:
		case TERMKEY_RES_ERROR:
			return ret;

		case TERMKEY_RES_NONE:
			ret = termkey_advisereadable (tk);
			if (ret == TERMKEY_RES_ERROR)
				return ret;
			break;

		case TERMKEY_RES_AGAIN:
		{
			if (tk->is_closed)
				// We're closed now. Never going to get more bytes
				// so just go with what we have
				return termkey_getkey_force (tk, key);

			struct pollfd fd;
retry:
			fd.fd = tk->fd;
			fd.events = POLLIN;

			int pollret = poll (&fd, 1, tk->waittime);
			if (pollret == -1)
			{
				if (errno == EINTR && !(tk->flags & TERMKEY_FLAG_EINTR))
					goto retry;

				return TERMKEY_RES_ERROR;
			}

			if (fd.revents & (POLLIN | POLLHUP | POLLERR))
				ret = termkey_advisereadable (tk);
			else
				ret = TERMKEY_RES_NONE;

			if (ret == TERMKEY_RES_ERROR)
				return ret;
			if (ret == TERMKEY_RES_NONE)
				return termkey_getkey_force (tk, key);
		}
		}
	}

	/* UNREACHABLE */
}

termkey_result_t
termkey_advisereadable (termkey_t *tk)
{
	if (tk->fd == -1)
	{
		errno = EBADF;
		return TERMKEY_RES_ERROR;
	}

	if (tk->buffstart)
	{
		memmove (tk->buffer, tk->buffer + tk->buffstart, tk->buffcount);
		tk->buffstart = 0;
	}

	/* Not expecting it ever to be greater but doesn't hurt to handle that */
	if (tk->buffcount >= tk->buffsize)
	{
		errno = ENOMEM;
		return TERMKEY_RES_ERROR;
	}

	ssize_t len;
retry:
	len = read (tk->fd, tk->buffer + tk->buffcount,
		tk->buffsize - tk->buffcount);

	if (len == -1)
	{
		if (errno == EAGAIN)
			return TERMKEY_RES_NONE;
		if (errno == EINTR && !(tk->flags & TERMKEY_FLAG_EINTR))
			goto retry;
		return TERMKEY_RES_ERROR;
	}
	if (len < 1)
	{
		tk->is_closed = true;
		return TERMKEY_RES_NONE;
	}
	tk->buffcount += len;
	return TERMKEY_RES_AGAIN;
}

size_t
termkey_push_bytes (termkey_t *tk, const char *bytes, size_t len)
{
	if (tk->buffstart)
	{
		memmove (tk->buffer, tk->buffer + tk->buffstart, tk->buffcount);
		tk->buffstart = 0;
	}

	/* Not expecting it ever to be greater but doesn't hurt to handle that */
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

termkey_sym_t
termkey_register_keyname (termkey_t *tk, termkey_sym_t sym, const char *name)
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
termkey_get_keyname (termkey_t *tk, termkey_sym_t sym)
{
	if (sym == TERMKEY_SYM_UNKNOWN)
		return "UNKNOWN";
	if (sym < tk->nkeynames)
		return tk->keynames[sym];
	return "UNKNOWN";
}

static const char *
termkey_lookup_keyname_format (termkey_t *tk,
	const char *str, termkey_sym_t *sym, termkey_format_t format)
{
	/* We store an array, so we can't do better than a linear search. Doesn't
	 * matter because user won't be calling this too often */

	for (*sym = 0; *sym < tk->nkeynames; (*sym)++)
	{
		const char *thiskey = tk->keynames[*sym];
		if (!thiskey)
			continue;
		size_t len = strlen (thiskey);
		if (format & TERMKEY_FORMAT_LOWERSPACE)
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
termkey_lookup_keyname (termkey_t *tk, const char *str, termkey_sym_t *sym)
{
	return termkey_lookup_keyname_format (tk, str, sym, 0);
}

termkey_sym_t
termkey_keyname2sym (termkey_t *tk, const char *keyname)
{
	termkey_sym_t sym;
	const char *endp = termkey_lookup_keyname (tk, keyname, &sym);
	if (!endp || endp[0])
		return TERMKEY_SYM_UNKNOWN;
	return sym;
}

static termkey_sym_t
register_c0 (termkey_t *tk,
	termkey_sym_t sym, unsigned char ctrl, const char *name)
{
	return register_c0_full (tk, sym, 0, 0, ctrl, name);
}

static termkey_sym_t
register_c0_full (termkey_t *tk, termkey_sym_t sym,
	int modifier_set, int modifier_mask, unsigned char ctrl, const char *name)
{
	if (ctrl >= 0x20)
	{
		errno = EINVAL;
		return -1;
	}

	if (name)
		sym = termkey_register_keyname (tk, sym, name);

	tk->c0[ctrl].sym = sym;
	tk->c0[ctrl].modifier_set = modifier_set;
	tk->c0[ctrl].modifier_mask = modifier_mask;
	return sym;
}

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

size_t
termkey_strfkey (termkey_t *tk, char *buffer, size_t len,
	termkey_key_t *key, termkey_format_t format)
{
	size_t pos = 0;
	size_t l = 0;

	struct modnames *mods = &modnames[
		!!(format & TERMKEY_FORMAT_LONGMOD) +
		!!(format & TERMKEY_FORMAT_ALTISMETA) * 2 +
		!!(format & TERMKEY_FORMAT_LOWERMOD) * 4];

	int wrapbracket = (format & TERMKEY_FORMAT_WRAPBRACKET) &&
		(key->type != TERMKEY_TYPE_KEY || key->modifiers != 0);

	char sep = (format & TERMKEY_FORMAT_SPACEMOD) ? ' ' : '-';

	if (format & TERMKEY_FORMAT_CARETCTRL &&
		key->type == TERMKEY_TYPE_KEY &&
 		key->modifiers == TERMKEY_KEYMOD_CTRL)
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
			if(l <= 0)
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

	if (key->modifiers & TERMKEY_KEYMOD_ALT)
	{
		l = snprintf (buffer + pos, len - pos, "%s%c", mods->alt, sep);
		if (l <= 0)
			return pos;
		pos += l;
	}
	if (key->modifiers & TERMKEY_KEYMOD_CTRL)
	{
		l = snprintf (buffer + pos, len - pos, "%s%c", mods->ctrl, sep);
		if (l <= 0)
			return pos;
		pos += l;
	}
	if (key->modifiers & TERMKEY_KEYMOD_SHIFT)
	{
		l = snprintf (buffer + pos, len - pos, "%s%c", mods->shift, sep);
		if (l <= 0)
			return pos;
		pos += l;
	}

	switch (key->type)
	{
	case TERMKEY_TYPE_KEY:
		if (!key->multibyte[0]) // In case of user-supplied key structures
			fill_multibyte (tk, key);
		l = snprintf (buffer + pos, len - pos, "%s", key->multibyte);
		break;
	case TERMKEY_TYPE_KEYSYM:
	{
		const char *name = termkey_get_keyname (tk, key->code.sym);
		if (format & TERMKEY_FORMAT_LOWERSPACE)
			l = snprint_cameltospaces (buffer + pos, len - pos, name);
		else
			l = snprintf (buffer + pos, len - pos, "%s", name);
		break;
	}
	case TERMKEY_TYPE_FUNCTION:
		l = snprintf (buffer + pos, len - pos, "%c%d",
			(format & TERMKEY_FORMAT_LOWERSPACE ? 'f' : 'F'), key->code.number);
		break;
	case TERMKEY_TYPE_MOUSE:
	{
		termkey_mouse_event_t ev;
		int button;
		int line, col;
		termkey_interpret_mouse (tk, key, &ev, &button, &line, &col);

		static const char *evnames[] =
			{ "Unknown", "Press", "Drag", "Release" };
		l = snprintf (buffer + pos, len - pos,
			"Mouse%s(%d)", evnames[ev], button);
		if (format & TERMKEY_FORMAT_MOUSE_POS)
		{
			if (l <= 0)
				return pos;
			pos += l;
			l = snprintf (buffer + pos, len - pos, " @ (%u,%u)", col, line);
		}
		break;
	}
	case TERMKEY_TYPE_POSITION:
		l = snprintf (buffer + pos, len - pos, "Position");
		break;
	case TERMKEY_TYPE_MODEREPORT:
	{
		int initial, mode, value;
		termkey_interpret_modereport (tk, key, &initial, &mode, &value);
		if (initial)
			l = snprintf (buffer + pos, len - pos,
				"Mode(%c%d=%d)", initial, mode, value);
		else
			l = snprintf (buffer + pos, len - pos,
				"Mode(%d=%d)", mode, value);
		break;
	}
	case TERMKEY_TYPE_UNKNOWN_CSI:
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

const char *
termkey_strpkey (termkey_t *tk,
	const char *str, termkey_key_t *key, termkey_format_t format)
{
	struct modnames *mods = &modnames[
		!!(format & TERMKEY_FORMAT_LONGMOD) +
		!!(format & TERMKEY_FORMAT_ALTISMETA) * 2 +
		!!(format & TERMKEY_FORMAT_LOWERMOD) * 4];

	key->modifiers = 0;

	if ((format & TERMKEY_FORMAT_CARETCTRL) && str[0] == '^' && str[1])
	{
		str = termkey_strpkey (tk,
			str + 1, key, format & ~TERMKEY_FORMAT_CARETCTRL);

		if (!str
		 || key->type != TERMKEY_TYPE_KEY
 		 || key->code.codepoint < '@'
		 || key->code.codepoint > '_'
 		 || key->modifiers != 0)
			return NULL;

		if (key->code.codepoint >= 'A'
		 && key->code.codepoint <= 'Z')
			key->code.codepoint += 0x20;
		key->modifiers = TERMKEY_KEYMOD_CTRL;
		fill_multibyte (tk, key);
		return (char *) str;
	}

	const char *sep_at;
	while ((sep_at = strchr (str,
		(format & TERMKEY_FORMAT_SPACEMOD) ? ' ' : '-')))
	{
		size_t n = sep_at - str;
		if (n == strlen (mods->alt) && !strncmp (mods->alt, str, n))
			key->modifiers |= TERMKEY_KEYMOD_ALT;
		else if (n == strlen (mods->ctrl) && !strncmp (mods->ctrl, str, n))
			key->modifiers |= TERMKEY_KEYMOD_CTRL;
		else if (n == strlen (mods->shift) && !strncmp (mods->shift, str, n))
			key->modifiers |= TERMKEY_KEYMOD_SHIFT;
		else
			break;

		str = sep_at + 1;
	}

	size_t nbytes;
	ssize_t snbytes;
	const char *endstr;

	if ((endstr = termkey_lookup_keyname_format
		(tk, str, &key->code.sym, format)))
	{
		key->type = TERMKEY_TYPE_KEYSYM;
		str = endstr;
	}
	else if (sscanf(str, "F%d%zn", &key->code.number, &snbytes) == 1)
	{
		key->type = TERMKEY_TYPE_FUNCTION;
		str += snbytes;
	}
	// Multibyte must be last
	else if (parse_multibyte (tk, (unsigned const char *) str, strlen (str),
		&key->code.codepoint, &nbytes) == TERMKEY_RES_KEY)
	{
		key->type = TERMKEY_TYPE_KEY;
		fill_multibyte (tk, key);
		str += nbytes;
	}
	// TODO: Consider mouse events?
	else
		return NULL;

	termkey_canonicalise (tk, key);
	return (char *) str;
}

int
termkey_keycmp (termkey_t *tk,
	const termkey_key_t *key1p, const termkey_key_t *key2p)
{
	/* Copy the key structs since we'll be modifying them */
	termkey_key_t key1 = *key1p, key2 = *key2p;

	termkey_canonicalise (tk, &key1);
	termkey_canonicalise (tk, &key2);

	if (key1.type != key2.type)
		return key1.type - key2.type;

	switch (key1.type)
	{
	case TERMKEY_TYPE_KEY:
		if (key1.code.codepoint != key2.code.codepoint)
			return key1.code.codepoint - key2.code.codepoint;
		break;
	case TERMKEY_TYPE_KEYSYM:
		if (key1.code.sym != key2.code.sym)
			return key1.code.sym - key2.code.sym;
		break;
	case TERMKEY_TYPE_FUNCTION:
	case TERMKEY_TYPE_UNKNOWN_CSI:
		if (key1.code.number != key2.code.number)
			return key1.code.number - key2.code.number;
		break;
	case TERMKEY_TYPE_MOUSE:
	{
		int cmp = strncmp (key1.code.mouse, key2.code.mouse, 4);
		if (cmp != 0)
			return cmp;
		break;
	}
	case TERMKEY_TYPE_POSITION:
	{
		int line1, col1, line2, col2;
		termkey_interpret_position (tk, &key1, &line1, &col1);
		termkey_interpret_position (tk, &key2, &line2, &col2);
		if (line1 != line2)
			return line1 - line2;
		return col1 - col2;
	}
	case TERMKEY_TYPE_MODEREPORT:
	{
		int initial1, initial2, mode1, mode2, value1, value2;
		termkey_interpret_modereport (tk, &key1, &initial1, &mode1, &value1);
		termkey_interpret_modereport (tk, &key2, &initial2, &mode2, &value2);
		if (initial1 != initial2)
			return initial1 - initial2;
		if (mode1 != mode2)
			return mode1 - mode2;
		return value1 - value2;
	}
	}
	return key1.modifiers - key2.modifiers;
}


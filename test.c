D 2-Clause License
 *
 * Copyright (c) 2014-2016, Lazaros Koromilas <lostd@2f30.org>
 * Copyright (c) 2014-2016, Dimitris Papastamos <sin@2f30.org>
 * Copyright (c) 2016-2019, Arun Prakash Jana <engineerarun@gmail.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice, this
 *   list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifdef __linux__
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#if defined(__arm__) || defined(__i386__)
#define _FILE_OFFSET_BITS 64 /* Support large files on 32-bit */
#endif
#include <sys/inotify.h>
#define LINUX_INOTIFY
#if !defined(__GLIBC__)
#include <sys/types.h>
#endif
#endif
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#if defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__APPLE__)
#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>
#define BSD_KQUEUE
#else
#include <sys/sysmacros.h>
#endif
#include <sys/wait.h>

#include <ctype.h>
#ifdef __linux__ /* Fix failure due to mvaddnwstr() */
#ifndef NCURSES_WIDECHAR
#define NCURSES_WIDECHAR 1
#endif
#elif defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__APPLE__)
#ifndef _XOPEN_SOURCE_EXTENDED
#define _XOPEN_SOURCE_EXTENDED
#endif
#endif
#ifndef __USE_XOPEN /* Fix wcswidth() failure, ncursesw/curses.h includes whcar.h on Ubuntu 14.04 */
#define __USE_XOPEN
#endif
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <libgen.h>
#include <limits.h>
#ifdef __gnu_hurd__
#define PATH_MAX 4096
#endif
#include <locale.h>
#include <pwd.h>
#include <regex.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
#ifndef __USE_XOPEN_EXTENDED
#define __USE_XOPEN_EXTENDED 1
#endif
#include <ftw.h>
#include <wchar.h>

#ifndef S_BLKSIZE
#define S_BLKSIZE 512 /* S_BLKSIZE is missing on Android NDK (Termux) */
#endif

#include "nnn.h"

#ifdef DEBUGMODE
static int DEBUG_FD;

static int
xprintf(int fd, const char *fmt, ...)
{
    char buf[BUFSIZ];
    int r;
    va_list ap;

    va_start(ap, fmt);
    r = vsnprintf(buf, sizeof(buf), fmt, ap);
    if (r > 0)
        r = write(fd, buf, r);
    va_end(ap);
    return r;
}

static int
enabledbg()
{
    FILE *fp = fopen("/tmp/nnn_debug", "w");

    if (!fp) {
        fprintf(stderr, "debug: open failed! (1)\n");

        fp = fopen("./nnn_debug", "w");
        if (!fp) {
            fprintf(stderr, "debug: open failed! (2)\n");
            return -1;
        }
    }

    DEBUG_FD = fileno(fp);
    if (DEBUG_FD == -1) {
        fprintf(stderr, "debug: open fd failed!\n");
        return -1;
    }

    return 0;
}

static void
disabledbg()
{
    close(DEBUG_FD);
}

#define DPRINTF_D(x) xprintf(DEBUG_FD, #x "=%d\n", x)
#define DPRINTF_U(x) xprintf(DEBUG_FD, #x "=%u\n", x)
#define DPRINTF_S(x) xprintf(DEBUG_FD, #x "=%s\n", x)
#define DPRINTF_P(x) xprintf(DEBUG_FD, #x "=%p\n", x)
#else
#define DPRINTF_D(x)
#define DPRINTF_U(x)
#define DPRINTF_S(x)
#define DPRINTF_P(x)
#endif /* DEBUGMODE */

/* Macro definitions */
#define VERSION "2.2"
#define GENERAL_INFO "BSD 2-Clause\nhttps://github.com/jarun/nnn"

#define LEN(x) (sizeof(x) / sizeof(*(x)))
#undef MIN
#define MIN(x, y) ((x) < (y) ? (x) : (y))
#define ISODD(x) ((x) & 1)
#define TOUPPER(ch) \
    (((ch) >= 'a' && (ch) <= 'z') ? ((ch) - 'a' + 'A') : (ch))
#define CMD_LEN_MAX (PATH_MAX + ((NAME_MAX + 1) << 1))
#define CURSR " > "
#define EMPTY "   "
#define CURSYM(flag) ((flag) ? CURSR : EMPTY)
#define FILTER '/'
#define REGEX_MAX 128
#define BM_MAX 10
#define ENTRY_INCR 64 /* Number of dir 'entry' structures to allocate per shot */
#define NAMEBUF_INCR 0x1000 /* 64 dir entries at once, avg. 64 chars per filename = 64*64B = 4KB */
#define DESCRIPTOR_LEN 32
#define _ALIGNMENT 0x10 /* 16-byte alignment */
#define _ALIGNMENT_MASK 0xF
#define SYMLINK_TO_DIR 0x1
#define HOME_LEN_MAX 64
#define CTX_MAX 4
#define DOT_FILTER_LEN 7
#define ASCII_MAX 128

/* Macros to define process spawn behaviour as flags */
#define F_NONE     0x00  /* no flag set */
#define F_MARKER   0x01  /* draw marker to indicate nnn spawned (e.g. shell) */
#define F_NOWAIT   0x02  /* don't wait for child process (e.g. file manager) */
#define F_NOTRACE  0x04  /* suppress stdout and strerr (no traces) */
#define F_SIGINT   0x08  /* restore default SIGINT handler */
#define F_NORMAL   0x80  /* spawn child process in non-curses regular CLI mode */

/* CRC8 macros */
#define WIDTH  (sizeof(unsigned char) << 3)
#define TOPBIT (1 << (WIDTH - 1))
#define POLYNOMIAL 0xD8  /* 11011 followed by 0's */
#define CRC8_TABLE_LEN 256

/* Volume info */
#define FREE 0
#define CAPACITY 1

/* Function macros */
#define exitcurses() endwin()
#define clearprompt() printmsg("")
#define printwarn() printmsg(strerror(errno))
#define istopdir(path) ((path)[1] == '\0' && (path)[0] == '/')
#define copycurname() xstrlcpy(lastname, dents[cur].name, NAME_MAX + 1)
#define settimeout() timeout(1000)
#define cleartimeout() timeout(-1)
#define errexit() printerr(__LINE__)
#define setdirwatch() (cfg.filtermode ? (presel = FILTER) : (dir_changed = TRUE))
/* We don't care about the return value from strcmp() */
#define xstrcmp(a, b)  (*(a) != *(b) ? -1 : strcmp((a), (b)))

#ifdef LINUX_INOTIFY
#define EVENT_SIZE (sizeof(struct inotify_event))
#define EVENT_BUF_LEN (1024 * (EVENT_SIZE + 16))
#elif defined(BSD_KQUEUE)
#define NUM_EVENT_SLOTS 1
#define NUM_EVENT_FDS 1
#endif

/* TYPE DEFINITIONS */
typedef unsigned long ulong;
typedef unsigned int uint;
typedef unsigned char uchar;
typedef unsigned short ushort;

/* STRUCTURES */

/* Directory entry */
typedef struct entry {
    char *name;
    time_t t;
    off_t size;
    blkcnt_t blocks; /* number of 512B blocks allocated */
    mode_t mode;
    ushort nlen; /* Length of file name; can be uchar (< NAME_MAX + 1) */
    uchar flags; /* Flags specific to the file */
} __attribute__ ((packed, aligned(_ALIGNMENT))) *pEntry;

/* Bookmark */
typedef struct {
    int key;
    char *loc;
} bm;

/* Settings */
typedef struct {
    uint filtermode : 1;  /* Set to enter filter mode */
    uint mtimeorder : 1;  /* Set to sort by time modified */
    uint sizeorder  : 1;  /* Set to sort by file size */
    uint apparentsz : 1;  /* Set to sort by apparent size (disk usage) */
    uint blkorder   : 1;  /* Set to sort by blocks used (disk usage) */
    uint showhidden : 1;  /* Set to show hidden files */
    uint copymode   : 1;  /* Set when copying files */
    uint autoselect : 1;  /* Auto-select dir in nav-as-you-type mode */
    uint showdetail : 1;  /* Clear to show fewer file info */
    uint showcolor  : 1;  /* Set to show dirs in blue */
    uint dircolor   : 1;  /* Current status of dir color */
    uint metaviewer : 1;  /* Index of metadata viewer in utils[] */
    uint ctxactive  : 1;  /* Context active or not */
    uint reserved   : 8;
    /* The following settings are global */
    uint curctx     : 2;  /* Current context number */
    uint picker     : 1;  /* Write selection to user-specified file */
    uint pickraw    : 1;  /* Write selection to sdtout before exit */
    uint nonavopen  : 1;  /* Open file on right arrow or `l` */
    uint useeditor  : 1;  /* Use VISUAL to open text files */
    uint runscript  : 1;  /* Choose script to run mode */
    uint runctx     : 2;  /* The context in which script is to be run */
    uint restrict0b : 1;  /* Restrict 0-byte file opening */
    uint filter_re  : 1;  /* Use regex filters */
} settings;

/* Contexts or workspaces */
typedef struct {
    char c_path[PATH_MAX]; /* Current dir */
    char c_init[PATH_MAX]; /* Initial dir */
    char c_last[PATH_MAX]; /* Last visited dir */
    char c_name[NAME_MAX + 1]; /* Current file name */
    settings c_cfg; /* Current configuration */
    uint color; /* Color code for directories */
} context;

/* GLOBALS */

/* Configuration, contexts */
static settings cfg = {0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
static context g_ctx[CTX_MAX] __attribute__ ((aligned));

static struct entry *dents;
static char *pnamebuf, *pcopybuf;
static int ndents, cur, total_dents = ENTRY_INCR;
static uint idle;
static uint idletimeout, copybufpos, copybuflen;
static char *opener;
static char *copier;
static char *editor;
static char *pager, *pager_arg;
static char *shell, *shell_arg;
static char *scriptpath;
static blkcnt_t ent_blocks;
static blkcnt_t dir_blocks;
static ulong num_files;
static uint open_max;
static bm bookmark[BM_MAX];
static size_t g_tmpfplen; /* path to tmp files for copy without X, keybind help and file stats */
static uchar g_crc;
static uchar BLK_SHIFT = 9;

/* CRC data */
static uchar crc8table[CRC8_TABLE_LEN] __attribute__ ((aligned));

/* For use in functions which are isolated and don't return the buffer */
static char g_buf[CMD_LEN_MAX] __attribute__ ((aligned));

/* Buffer for file path copy file */
static char g_cppath[PATH_MAX] __attribute__ ((aligned));

/* Buffer to store tmp file path */
static char g_tmpfpath[HOME_LEN_MAX] __attribute__ ((aligned));

#ifdef LINUX_INOTIFY
static int inotify_fd, inotify_wd = -1;
static uint INOTIFY_MASK = IN_ATTRIB | IN_CREATE | IN_DELETE | IN_DELETE_SELF | IN_MODIFY | IN_MOVE_SELF | IN_MOVED_FROM | IN_MOVED_TO;
#elif defined(BSD_KQUEUE)
static int kq, event_fd = -1;
static struct kevent events_to_monitor[NUM_EVENT_FDS];
static uint KQUEUE_FFLAGS = NOTE_DELETE | NOTE_EXTEND | NOTE_LINK | NOTE_RENAME | NOTE_REVOKE | NOTE_WRITE;
static struct timespec gtimeout;
#endif

/* Replace-str for xargs on different platforms */
#if defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__APPLE__)
#define REPLACE_STR 'J'
#elif defined(__linux__) || defined(__CYGWIN__)
#define REPLACE_STR 'I'
#else
#define REPLACE_STR 'I'
#endif

/* Options to identify file mime */
#ifdef __APPLE__
#define FILE_OPTS "-bI"
#else
#define FILE_OPTS "-bi"
#endif

/* Macros for utilities */
#define MEDIAINFO 0
#define EXIFTOOL 1
#define OPENER 2
#define ATOOL 3
#define APACK 4
#define VIDIR 5
#define LOCKER 6
#define UNKNOWN 7

/* Utilities to open files, run actions */
static char * const utils[] = {
    "mediainfo",
    "exiftool",
#ifdef __APPLE__
    "/usr/bin/open",
#elif defined __CYGWIN__
    "cygstart",
#else
    "xdg-open",
#endif
    "atool",
    "apack",
    "vidir",
#ifdef __APPLE__
    "bashlock",
#elif defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
    "lock",
#else
    "vlock",
#endif
    "UNKNOWN"
};

/* Common strings */
#define STR_NFTWFAIL_ID 0
#define STR_NOHOME_ID 1
#define STR_INPUT_ID 2
#define STR_INVBM_KEY 3
#define STR_DATE_ID 4
#define STR_UNSAFE 5
#define STR_TMPFILE 6

static const char * const messages[] = {
    "nftw failed",
    "HOME not set",
    "no traversal",
    "invalid key",
    "%F %T %z",
    "unsafe cmd",
    "/.nnnXXXXXX",
};

/* Supported config env vars */
#define NNN_BMS 0
#define NNN_OPENER 1
#define NNN_CONTEXT_COLORS 2
#define NNN_IDLE_TIMEOUT 3
#define NNN_COPIER 4
#define NNN_SCRIPT 5
#define NNN_NOTE 6
#define NNN_TMPFILE 7
#define NNN_USE_EDITOR 8
#define NNN_SHOW_HIDDEN 9
#define NNN_NO_AUTOSELECT 10
#define NNN_RESTRICT_NAV_OPEN 11
#define NNN_RESTRICT_0B 12
#define NNN_PLAIN_FILTER 13

static const char * const env_cfg[] = {
    "NNN_BMS",
    "NNN_OPENER",
    "NNN_CONTEXT_COLORS",
    "NNN_IDLE_TIMEOUT",
    "NNN_COPIER",
    "NNN_SCRIPT",
    "NNN_NOTE",
    "NNN_TMPFILE",
    "NNN_USE_EDITOR",
    "NNN_SHOW_HIDDEN",
    "NNN_NO_AUTOSELECT",
    "NNN_RESTRICT_NAV_OPEN",
    "NNN_RESTRICT_0B",
    "NNN_PLAIN_FILTER",
};

/* Required env vars */
#define PWD 0
#define SHELL 1
#define SHLVL 2
#define VISUAL 3
#define EDITOR 4
#define PAGER 5

static const char * const envs[] = {
    "PWD",
    "SHELL",
    "SHLVL",
    "VISUAL",
    "EDITOR",
    "PAGER",
};

/* Forward declarations */
static void redraw(char *path);
static void spawn(const char *file, const char *arg1, const char *arg2, const char *dir, uchar flag);
static int (*nftw_fn) (const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf);

/* Functions */

/*
 * CRC8 source:
 *   https://barrgroup.com/Embedded-Systems/How-To/CRC-Calculation-C-Code
 */
static void crc8init()
{
    uchar remainder, bit;
    uint dividend;

    /* Compute the remainder of each possible dividend  */
    for (dividend = 0; dividend < CRC8_TABLE_LEN; ++dividend) {
        /* Start with the dividend followed by zeros */
        remainder = dividend << (WIDTH - 8);

        /* Perform modulo-2 division, a bit at a time */
        for (bit = 8; bit > 0; --bit) {
            /* Try to divide the current data bit */
            if (remainder & TOPBIT)
                remainder = (remainder << 1) ^ POLYNOMIAL;
            else
                remainder = (remainder << 1);
        }

        /* Store the result into the table */
        crc8table[dividend] = remainder;
    }
}

static uchar crc8fast(uchar const message[], size_t n)
{
    static uchar data, remainder;
    static size_t byte;

    /* Divide the message by the polynomial, a byte at a time */
    for (remainder = byte = 0; byte < n; ++byte) {
        data = message[byte] ^ (remainder >> (WIDTH - 8));
        remainder = crc8table[data] ^ (remainder << 8);
    }

    /* The final remainder is the CRC */
    return remainder;
}

/* Messages show up at the bottom */
static void printmsg(const char *msg)
{
    mvprintw(LINES - 1, 0, "%s\n", msg);
}

/* Kill curses and display error before exiting */
static void printerr(int linenum)
{
    exitcurses();
    fprintf(stderr, "line %d: (%d) %s\n", linenum, errno, strerror(errno));
    if (!cfg.picker && g_cppath[0])
        unlink(g_cppath);
    free(pcopybuf);
    exit(1);
}

/* Print prompt on the last line */
static void printprompt(const char *str)
{
    clearprompt();
    printw(str);
}

static int get_input(const char *prompt)
{
    if (prompt)
        printprompt(prompt);
    cleartimeout();
    int r = getch();
    settimeout();
    return r;
}

static char confirm_force()
{
    int r = get_input("use force? (y/Y)");
    if (r == 'y' || r == 'Y')
        return 'f'; /* forceful */

    return 'i'; /* interactive */
}

/* Increase the limit on open file descriptors, if possible */
static rlim_t max_openfds()
{
    struct rlimit rl;
    rlim_t limit = getrlimit(RLIMIT_NOFILE, &rl);

    if (limit != 0)
        return 32;

    limit = rl.rlim_cur;
    rl.rlim_cur = rl.rlim_max;

    /* Return ~75% of max possible */
    if (setrlimit(RLIMIT_NOFILE, &rl) == 0) {
        limit = rl.rlim_max - (rl.rlim_max >> 2);
        /*
         * 20K is arbitrary. If the limit is set to max possible
         * value, the memory usage increases to more than double.
         */
        return limit > 20480 ?  20480 : limit;
    }

    return limit;
}

/*
 * Wrapper to realloc()
 * Frees current memory if realloc() fails and returns NULL.
 *
 * As per the docs, the *alloc() family is supposed to be memory aligned:
 * Ubuntu: http://manpages.ubuntu.com/manpages/xenial/man3/malloc.3.html
 * macOS: https://developer.apple.com/legacy/library/documentation/Darwin/Reference/ManPages/man3/malloc.3.html
 */
static void *xrealloc(void *pcur, size_t len)
{
    static void *pmem;

    pmem = realloc(pcur, len);
    if (!pmem)
        free(pcur);

    return pmem;
}

/*
 * Just a safe strncpy(3)
 * Always null ('\0') terminates if both src and dest are valid pointers.
 * Returns the number of bytes copied including terminating null byte.
 */
static size_t xstrlcpy(char *dest, const char *src, size_t n)
{
    static ulong *s, *d;
    static size_t len, blocks;
    static const uint lsize = sizeof(ulong);
    static const uint _WSHIFT = (sizeof(ulong) == 8) ? 3 : 2;

    if (!src || !dest || !n)
        return 0;

    len = strlen(src) + 1;
    if (n > len)
        n = len;
    else if (len > n)
        /* Save total number of bytes to copy in len */
        len = n;

    /*
     * To enable -O3 ensure src and dest are 16-byte aligned
     * More info: http://www.felixcloutier.com/x86/MOVDQA.html
     */
    if ((n >= lsize) && (((ulong)src & _ALIGNMENT_MASK) == 0 &&
        ((ulong)dest & _ALIGNMENT_MASK) == 0)) {
        s = (ulong *)src;
        d = (ulong *)dest;
        blocks = n >> _WSHIFT;
        n &= lsize - 1;

        while (blocks) {
            *d = *s;
            ++d, ++s;
            --blocks;
        }

        if (!n) {
            dest = (char *)d;
            *--dest = '\0';
            return len;
        }

        src = (char *)s;
        dest = (char *)d;
    }

    while (--n && (*dest = *src))
        ++dest, ++src;

    if (!n)
        *dest = '\0';

    return len;
}

/*
 * The poor man's implementation of memrchr(3).
 * We are only looking for '/' in this program.
 * And we are NOT expecting a '/' at the end.
 * Ideally 0 < n <= strlen(s).
 */
static void *xmemrchr(uchar *s, uchar ch, size_t n)
{
    static uchar *ptr;

    if (!s || !n)
        return NULL;

    ptr = s + n;

    do {
        --ptr;

        if (*ptr == ch)
            return ptr;
    } while (s != ptr);

    return NULL;
}

/*
 * The following dirname(3) implementation does not
 * modify the input. We use a copy of the original.
 *
 * Modified from the glibc (GNU LGPL) version.
 */
static char *xdirname(const char *path)
{
    static char * const buf = g_buf, *last_slash, *runp;

    xstrlcpy(buf, path, PATH_MAX);

    /* Find last '/'. */
    last_slash = xmemrchr((uchar *)buf, '/', strlen(buf));

    if (last_slash != NULL && last_slash != buf && last_slash[1] == '\0') {
        /* Determine whether all remaining characters are slashes. */
        for (runp = last_slash; runp != buf; --runp)
            if (runp[-1] != '/')
                break;

        /* The '/' is the last character, we have to look further. */
        if (runp != buf)
            last_slash = xmemrchr((uchar *)buf, '/', runp - buf);
    }

    if (last_slash != NULL) {
        /* Determine whether all remaining characters are slashes. */
        for (runp = last_slash; runp != buf; --runp)
            if (runp[-1] != '/')
                break;

        /* Terminate the buffer. */
        if (runp == buf) {
            /* The last slash is the first character in the string.
             * We have to return "/". As a special case we have to
             * return "//" if there are exactly two slashes at the
             * beginning of the string. See XBD 4.10 Path Name
             * Resolution for more information.
             */
            if (last_slash == buf + 1)
                ++last_slash;
            else
                last_slash = buf + 1;
        } else
            last_slash = runp;

        last_slash[0] = '\0';
    } else {
        /* This assignment is ill-designed but the XPG specs require to
         * return a string containing "." in any case no directory part
         * is found and so a static and constant string is required.
         */
        buf[0] = '.';
        buf[1] = '\0';
    }

    return buf;
}

static char *xbasename(char *path)
{
    static char *base;

    base = xmemrchr((uchar *)path, '/', strlen(path));
    return base ? base + 1 : path;
}

/* Writes buflen char(s) from buf to a file */
static void writecp(const char *buf, const size_t buflen)
{
    static FILE *fp;

    if (cfg.pickraw)
        return;

    if (!g_cppath[0])
        return;

    fp = fopen(g_cppath, "w");

    if (fp) {
        fwrite(buf, 1, buflen, fp);
        fclose(fp);
    } else
        printwarn();
}

static bool appendfpath(const char *path, const size_t len)
{
    if ((copybufpos >= copybuflen) || ((len + 3) > (copybuflen - copybufpos))) {
        copybuflen += PATH_MAX;
        pcopybuf = xrealloc(pcopybuf, copybuflen);
        if (!pcopybuf) {
            printmsg("no memory!");
            return FALSE;
        }
    }

    /* Enabling the following will miss files with newlines */
    /* if (copybufpos)
        pcopybuf[copybufpos - 1] = '\n'; */

    copybufpos += xstrlcpy(pcopybuf + copybufpos, path, len);

    return TRUE;
}

/* Write selected file paths to fd, linefeed separated */
static ssize_t selectiontofd(int fd)
{
    uint lastpos = copybufpos - 1;
    char *pbuf = pcopybuf;
    ssize_t pos = 0, len, r;

    while (pos <= lastpos) {
        len = strlen(pbuf);
        pos += len;

        r = write(fd, pbuf, len);
        if (r != len)
            return pos;

        if (pos <= lastpos) {
            if (write(fd, "\n", 1) != 1)
                return pos;
            pbuf += pos + 1;
        }
        ++pos;
    }

    return pos;
}

static bool showcplist()
{
    int fd;
    ssize_t pos;

    if (!copybufpos)
        return FALSE;

    if (g_tmpfpath[0])
        xstrlcpy(g_tmpfpath + g_tmpfplen - 1, messages[STR_TMPFILE],
             HOME_LEN_MAX - g_tmpfplen);
    else {
        printmsg(messages[STR_NOHOME_ID]);
        return -1;
    }

    fd = mkstemp(g_tmpfpath);
    if (fd == -1)
        return FALSE;

    pos = selectiontofd(fd);

    close(fd);
    if (pos && pos == copybufpos)
        spawn(pager, pager_arg, g_tmpfpath, NULL, F_NORMAL);
    unlink(g_tmpfpath);
    return TRUE;
}

/* Initialize curses mode */
static bool initcurses(void)
{
    if (cfg.picker) {
        if (!newterm(NULL, stderr, stdin)) {
            fprintf(stderr, "newterm!\n");
            return FALSE;
        }
    } else if (!initscr()) {
        char *term = getenv("TERM");

        if (term != NULL)
            fprintf(stderr, "error opening TERM: %s\n", term);
        else
            fprintf(stderr, "initscr!\n");
        return FALSE;
    }

    cbreak();
    noecho();
    nonl();
    intrflush(stdscr, FALSE);
    keypad(stdscr, TRUE);
    curs_set(FALSE); /* Hide cursor */
    start_color();
    use_default_colors();
    if (cfg.showcolor) {
        init_pair(1, g_ctx[0].color, -1);
        init_pair(2, g_ctx[1].color, -1);
        init_pair(3, g_ctx[2].color, -1);
        init_pair(4, g_ctx[3].color, -1);
    }
    settimeout(); /* One second */
    set_escdelay(25);
    return TRUE;
}

/*
 * Spawns a child process. Behaviour can be controlled using flag.
 * Limited to 2 arguments to a program, flag works on bit set.
 */
static void spawn(const char *file, const char *arg1, const char *arg2, const char *dir, uchar flag)
{
    static const char *shlvl;
    static pid_t pid;
    static int status;

    /* Swap args if the first arg is NULL and second isn't */
    if (!arg1 && arg2) {
        shlvl = arg1;
        arg1 = arg2;
        arg2 = shlvl;
    }

    if (flag & F_NORMAL)
        exitcurses();

    pid = fork();
    if (pid == 0) {
        if (dir != NULL)
            status = chdir(dir);

        shlvl = getenv(envs[SHLVL]);

        /* Show a marker (to indicate nnn spawned shell) */
        if (flag & F_MARKER && shlvl != NULL) {
            fprintf(stdout, "\n +-++-++-+\n | n n n |\n +-++-++-+\n\n");
            fprintf(stdout, "Next shell level: %d\n", atoi(shlvl) + 1);
        }

        /* Suppress stdout and stderr */
        if (flag & F_NOTRACE) {
            int fd = open("/dev/null", O_WRONLY, 0200);

            dup2(fd, 1);
            dup2(fd, 2);
            close(fd);
        }

        if (flag & F_NOWAIT) {
            signal(SIGHUP, SIG_IGN);
            signal(SIGPIPE, SIG_IGN);
            setsid();
        }

        if (flag & F_SIGINT)
            signal(SIGINT, SIG_DFL);

        execlp(file, file, arg1, arg2, NULL);
        _exit(1);
    } else {
        if (!(flag & F_NOWAIT))
            /* Ignore interruptions */
            while (waitpid(pid, &status, 0) == -1)
                DPRINTF_D(status);

        DPRINTF_D(pid);
        if (flag & F_NORMAL)
            refresh();
    }
}

/*
 * Quotes argument and spawns a shell command
 * Uses g_buf
 */
static bool quote_run_sh_cmd(const char *cmd, const char *arg, const char *path)
{
    const char *ptr;
    size_t r;

    if (!cmd)
        return FALSE;

    r = xstrlcpy(g_buf, cmd, CMD_LEN_MAX);

    if (arg) {
        if (r >= CMD_LEN_MAX - 4) { /* space for at least 4 chars - space'c' */
            printmsg(messages[STR_UNSAFE]);
            return FALSE;
        }

        for (ptr = arg; *ptr; ++ptr)
            if (*ptr == '\'') {
                printmsg(messages[STR_UNSAFE]);
                return FALSE;
            }

        g_buf[r - 1] = ' ';
        g_buf[r] = '\'';
        r += xstrlcpy(g_buf + r + 1, arg, CMD_LEN_MAX - 1 - r);
        if (r >= CMD_LEN_MAX - 1) {
            printmsg(messages[STR_UNSAFE]);
            return FALSE;
        }

        g_buf[r] = '\'';
        g_buf[r + 1] = '\0';
    }

    DPRINTF_S(g_buf);
    spawn("sh", "-c", g_buf, path, F_NORMAL);
    return TRUE;
}

/* Get program name from env var, else return fallback program */
static char *xgetenv(const char *name, char *fallback)
{
    static char *value;

    if (name == NULL)
        return fallback;

    value = getenv(name);

    return value && value[0] ? value : fallback;
}

/*
 * Parse a string to get program and argument
 * NOTE: original string may be modified
 */
static void getprogarg(char *prog, char **arg)
{
    char *argptr;

    while (*prog && !isblank(*prog))
        ++prog;

    if (*prog) {
        *prog = '\0';
        *arg = ++prog;
        argptr = *arg;

        /* Make sure there are no more args */
        while (*argptr) {
            if (isblank(*argptr)) {
                fprintf(stderr, "Too many args [%s]\n", prog);
                exit(1);
            }
            ++argptr;
        }
    }
}

/* Check if a dir exists, IS a dir and is readable */
static bool xdiraccess(const char *path)
{
    static DIR *dirp;

    dirp = opendir(path);
    if (dirp == NULL) {
        printwarn();
        return FALSE;
    }

    closedir(dirp);
    return TRUE;
}

static int digit_compare(const char *a, const char *b)
{
    while (*a && *b && *a == *b)
        ++a, ++b;

    return *a - *b;
}

/*
 * We assume none of the strings are NULL.
 *
 * Let's have the logic to sort numeric names in numeric order.
 * E.g., the order '1, 10, 2' doesn't make sense to human eyes.
 *
 * If the absolute numeric values are same, we fallback to alphasort.
 */
static int xstricmp(const char * const s1, const char * const s2)
{
    static const char *c1, *c2, *m1, *m2;
    static int count1, count2, bias;

    static char sign[2];

    count1 = 0;
    count2 = 0;
    sign[0] = '+';
    sign[1] = '+';

    c1 = s1;
    while (isspace(*c1))
        ++c1;

    c2 = s2;
    while (isspace(*c2))
        ++c2;

    if (*c1 == '-' || *c1 == '+') {
        if (*c1 == '-')
            sign[0] = '-';
        ++c1;
    }

    if (*c2 == '-' || *c2 == '+') {
        if (*c2 == '-')
            sign[1] = '-';
        ++c2;
    }

    if ((*c1 >= '0' && *c1 <= '9') && (*c2 >= '0' && *c2 <= '9')) {
        while (*c1 == '0')
            ++c1;
        m1 = c1;

        while (*c2 == '0')
            ++c2;
        m2 = c2;

        while (*c1 >= '0' && *c1 <= '9') {
            ++count1;
            ++c1;
        }
        while (isspace(*c1))
            ++c1;

        while (*c2 >= '0' && *c2 <= '9') {
            ++count2;
            ++c2;
        }
        while (isspace(*c2))
            ++c2;

        if (*c1 && !*c2)
            return 1;

        if (!*c1 && *c2)
            return -1;

        if (!*c1 && !*c2) {
            if (sign[0] != sign[1])
                return ((sign[0] == '+') ? 1 : -1);

            if (count1 > count2)
                return 1;

            if (count1 < count2)
                return -1;

            bias = digit_compare(m1, m2);
            if (bias)
                return bias;
        }
    }

    return strcoll(s1, s2);
}

/* Return the integer value of a char representing HEX */
static char xchartohex(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';

    c = TOUPPER(c);
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;

    return c;
}

static int setfilter(regex_t *regex, char *filter)
{
    static size_t len;
    static int r;

    r = regcomp(regex, filter, REG_NOSUB | REG_EXTENDED | REG_ICASE);
    if (r != 0 && filter && filter[0] != '\0') {
        len = COLS;
        if (len > NAME_MAX)
            len = NAME_MAX;
        mvprintw(LINES - 1, 0, "regex error: %d\n", r);
    }

    return r;
}

static int visible_re(regex_t *regex, char *fname, char *fltr)
{
    return regexec(regex, fname, 0, NULL, 0) == 0;
}

static int visible_str(regex_t *regex, char *fname, char *fltr)
{
    return strcasestr(fname, fltr) != NULL;
}

static int (*filterfn)(regex_t *regex, char *fname, char *fltr) = &visible_re;

static int entrycmp(const void *va, const void *vb)
{
    static pEntry pa, pb;

    pa = (pEntry)va;
    pb = (pEntry)vb;

    /* Sort directories first */
    if (S_ISDIR(pb->mode) && !S_ISDIR(pa->mode))
        return 1;

    if (S_ISDIR(pa->mode) && !S_ISDIR(pb->mode))
        return -1;

    /* Do the actual sorting */
    if (cfg.mtimeorder)
        return pb->t - pa->t;

    if (cfg.sizeorder) {
        if (pb->size > pa->size)
            return 1;

        if (pb->size < pa->size)
            return -1;
    }

    if (cfg.blkorder) {
        if (pb->blocks > pa->blocks)
            return 1;

        if (pb->blocks < pa->blocks)
            return -1;
    }

    return xstricmp(pa->name, pb->name);
}

/*
 * Returns SEL_* if key is bound and 0 otherwise.
 * Also modifies the run and env pointers (used on SEL_{RUN,RUNARG}).
 * The next keyboard input can be simulated by presel.
 */
static int nextsel(int *presel)
{
    static int c;
    static uint i;
    static const uint len = LEN(bindings);
#ifdef LINUX_INOTIFY
    static char inotify_buf[EVENT_BUF_LEN];
#elif defined(BSD_KQUEUE)
    static struct kevent event_data[NUM_EVENT_SLOTS];
#endif
    c = *presel;

    if (c == 0) {
        c = getch();
        DPRINTF_D(c);
    } else {
        /* Unwatch dir if we are still in a filtered view */
#ifdef LINUX_INOTIFY
        if (*presel == FILTER && inotify_wd >= 0) {
            inotify_rm_watch(inotify_fd, inotify_wd);
            inotify_wd = -1;
        }
#elif defined(BSD_KQUEUE)
        if (*presel == FILTER && event_fd >= 0) {
            close(event_fd);
            event_fd = -1;
        }
#endif
        *presel = 0;
    }

    if (c == -1) {
        ++idle;

        /*
         * Do not check for directory changes in du mode.
         * A redraw forces du calculation.
         * Check for changes every odd second.
         */
#ifdef LINUX_INOTIFY
        if (!cfg.blkorder && inotify_wd >= 0 && idle & 1
            && read(inotify_fd, inotify_buf, EVENT_BUF_LEN) > 0)
#elif defined(BSD_KQUEUE)
        if (!cfg.blkorder && event_fd >= 0 && idle & 1
            && kevent(kq, events_to_monitor, NUM_EVENT_SLOTS,
                  event_data, NUM_EVENT_FDS, &gtimeout) > 0)
#endif
                c = CONTROL('L');
    } else
        idle = 0;

    for (i = 0; i < len; ++i)
        if (c == bindings[i].sym)
            return bindings[i].act;

    return 0;
}

/*
 * Move non-matching entries to the end
 */
static int fill(char* fltr, regex_t *re)
{
    static int count;
    static struct entry _dent, *pdent1, *pdent2;

    for (count = 0; count < ndents; ++count) {
        if (filterfn(re, dents[count].name, fltr) == 0) {
            if (count != --ndents) {
                pdent1 = &dents[count];
                pdent2 = &dents[ndents];

                *(&_dent) = *pdent1;
                *pdent1 = *pdent2;
                *pdent2 = *(&_dent);
                --count;
            }

            continue;
        }
    }

    return ndents;
}

static int matches(char *fltr)
{
    static regex_t re;

    /* Search filter */
    if (cfg.filter_re && setfilter(&re, fltr) != 0)
        return -1;

    ndents = fill(fltr, &re);
    if (cfg.filter_re)
        regfree(&re);
    if (!ndents)
        return 0;

    qsort(dents, ndents, sizeof(*dents), entrycmp);

    return 0;
}

static int filterentries(char *path)
{
    static char ln[REGEX_MAX] __attribute__ ((aligned));
    static wchar_t wln[REGEX_MAX] __attribute__ ((aligned));
    static wint_t ch[2] = {0};
    int r, total = ndents, oldcur = cur, len = 1;
    char *pln = ln + 1;

    ln[0] = wln[0] = FILTER;
    ln[1] = wln[1] = '\0';
    cur = 0;

    cleartimeout();
    curs_set(TRUE);
    printprompt(ln);

    while ((r = get_wch(ch)) != ERR) {
        switch (*ch) {
        case KEY_DC: // fallthrough
        case  KEY_BACKSPACE: // fallthrough
        case '\b': // fallthrough
        case CONTROL('L'): // fallthrough
        case 127: /* handle DEL */
            if (len == 1 && *ch != CONTROL('L')) {
                cur = oldcur;
                *ch = CONTROL('L');
                goto end;
            }

            if (*ch == CONTROL('L'))
                while (len > 1)
                    wln[--len] = '\0';
            else
                wln[--len] = '\0';

            if (len == 1)
                cur = oldcur;

            wcstombs(ln, wln, REGEX_MAX);
            ndents = total;
            if (matches(pln) != -1)
                redraw(path);

            printprompt(ln);
            continue;
        case 27: /* Exit filter mode on Escape */
            if (len == 1)
                cur = oldcur;
            *ch = CONTROL('L');
            goto end;
        }

        if (r == OK) {
            /* Handle all control chars in main loop */
            if (*ch < ASCII_MAX && keyname(*ch)[0] == '^' && *ch != '^') {
                if (len == 1)
                    cur = oldcur;
                goto end;
            }

            switch (*ch) {
            case '\r':  // with nonl(), this is ENTER key value
                if (len == 1) {
                    cur = oldcur;
                    goto end;
                }

                if (matches(pln) == -1)
                    goto end;

                redraw(path);
                goto end;
            case '?':  // '?' is an invalid regex, show help instead
                if (len == 1) {
                    cur = oldcur;
                    goto end;
                } // fallthrough
            default:
                /* Reset cur in case it's a repeat search */
                if (len == 1)
                    cur = 0;

                if (len == REGEX_MAX - 1)
                    break;

                wln[len] = (wchar_t)*ch;
                wln[++len] = '\0';
                wcstombs(ln, wln, REGEX_MAX);

                /* Forward-filtering optimization:
                 * - new matches can only be a subset of current matches.
                 */
                /* ndents = total; */

                if (matches(pln) == -1)
                    continue;

                /* If the only match is a dir, auto-select and cd into it */
                if (ndents == 1 && cfg.filtermode
                    && cfg.autoselect && S_ISDIR(dents[0].mode)) {
                    *ch = KEY_ENTER;
                    cur = 0;
                    goto end;
                }

                /*
                 * redraw() should be above the auto-select optimization, for
                 * the case where there's an issue with dir auto-select, say,
                 * due to a permission problem. The transition is _jumpy_ in
                 * case of such an error. However, we optimize for successful
                 * cases where the dir has permissions. This skips a redraw().
                 */
                redraw(path);
                printprompt(ln);
            }
        } else {
            if (len == 1)
                cur = oldcur;
            goto end;
        }
    }
end:
    curs_set(FALSE);
    settimeout();

    /* Return keys for navigation etc. */
    return *ch;
}

/* Show a prompt with input string and return the changes */
static char *xreadline(char *prefill, char *prompt)
{
    size_t len, pos;
    int x, y, r;
    wint_t ch[2] = {0};
    static wchar_t * const buf = (wchar_t *)g_buf;

    cleartimeout();
    printprompt(prompt);

    if (prefill) {
        DPRINTF_S(prefill);
        len = pos = mbstowcs(buf, prefill, NAME_MAX);
    } else
        len = (size_t)-1;

    if (len == (size_t)-1) {
        buf[0] = '\0';
        len = pos = 0;
    }

    getyx(stdscr, y, x);
    curs_set(TRUE);

    while (1) {
        buf[len] = ' ';
        mvaddnwstr(y, x, buf, len + 1);
        move(y, x + wcswidth(buf, pos));

        r = get_wch(ch);
        if (r != ERR) {
            if (r == OK) {
                switch (*ch) {
                case KEY_ENTER: // fallthrough
                case '\n': // fallthrough
                case '\r':
                    goto END;
                case 127: /* Handle DEL */ // fallthrough
                case '\b': /* rhel25 sends '\b' for backspace */
                    if (pos > 0) {
                        memmove(buf + pos - 1, buf + pos, (len - pos) << 2);
                        --len, --pos;
                    } // fallthrough
                case '\t': /* TAB breaks cursor position, ignore it */
                    continue;
                case CONTROL('L'):
                    clearprompt();
                    printprompt(prompt);
                    len = pos = 0;
                    continue;
                case CONTROL('A'):
                    pos = 0;
                    continue;
                case CONTROL('E'):
                    pos = len;
                    continue;
                case CONTROL('U'):
                    clearprompt();
                    printprompt(prompt);
                    memmove(buf, buf + pos, (len - pos) << 2);
                    len -= pos;
                    pos = 0;
                    continue;
                case 27: /* Exit prompt on Escape */
                    len = 0;
                    goto END;
                }

                /* Filter out all other control chars */
                if (*ch < ASCII_MAX && keyname(*ch)[0] == '^')
                    continue;

                if (pos < NAME_MAX - 1) {
                    memmove(buf + pos + 1, buf + pos, (len - pos) << 2);
                    buf[pos] = *ch;
                    ++len, ++pos;
                    continue;
                }
            } else {
                switch (*ch) {
                case KEY_LEFT:
                    if (pos > 0)
                        --pos;
                    break;
                case KEY_RIGHT:
                    if (pos < len)
                        ++pos;
                    break;
                case KEY_BACKSPACE:
                    if (pos > 0) {
                        memmove(buf + pos - 1, buf + pos, (len - pos) << 2);
                        --len, --pos;
                    }
                    break;
                case KEY_DC:
                    if (pos < len) {
                        memmove(buf + pos, buf + pos + 1,
                            (len - pos - 1) << 2);
                        --len;
                    }
                    break;
                default:
                    break;
                }
            }
        }
    }

END:
    curs_set(FALSE);
    settimeout();
    clearprompt();

    buf[len] = '\0';
    wcstombs(g_buf + ((NAME_MAX + 1) << 2), buf, NAME_MAX);
    return g_buf + ((NAME_MAX + 1) << 2);
}

/*
 * Updates out with "dir/name or "/name"
 * Returns the number of bytes copied including the terminating NULL byte
 */
static size_t mkpath(char *dir, char *name, char *out)
{
    static size_t len;

    /* Handle absolute path */
    if (name[0] == '/')
        return xstrlcpy(out, name, PATH_MAX);

    /* Handle root case */
    if (istopdir(dir))
        len = 1;
    else
        len = xstrlcpy(out, dir, PATH_MAX);

    out[len - 1] = '/';
    return (xstrlcpy(out + len, name, PATH_MAX - len) + len);
}

/*
 * Create symbolic/hard link(s) to file(s) in selection list
 * Returns the number of links created
 */
static int xlink(char *suffix, char *path, char *buf, int type)
{
    int count = 0;
    char *pbuf = pcopybuf, *fname;
    ssize_t pos = 0, len, r;
    int (*link_fn)(const char *, const char *) = NULL;

    /* Check if selection is empty */
    if (!copybufpos)
        return 0;

    if (type == 's') /* symbolic link */
        link_fn = &symlink;
    else if (type == 'h') /* hard link */
        link_fn = &link;
    else
        return -1;

    while (pos < copybufpos) {
        len = strlen(pbuf);
        fname = xbasename(pbuf);
        r = mkpath(path, fname, buf);
        xstrlcpy(buf + r - 1, suffix, PATH_MAX - r - 1);

        if (!link_fn(pbuf, buf))
            ++count;

        pos += len + 1;
        pbuf += len + 1;
    }

    return count;
}

static bool parsebmstr()
{
    int i = 0;
    char *bms = getenv(env_cfg[NNN_BMS]);
    if (!bms)
        return TRUE;

    while (*bms && i < BM_MAX) {
        bookmark[i].key = *bms;

        if (!*++bms) {
            bookmark[i].key = '\0';
            break;
        }

        if (*bms != ':')
            return FALSE; /* We support single char keys only */

        bookmark[i].loc = ++bms;
        if (bookmark[i].loc[0] == '\0' || bookmark[i].loc[0] == ';') {
            bookmark[i].key = '\0';
            break;
        }

        while (*bms && *bms != ';')
            ++bms;

        if (*bms)
            *bms = '\0';
        else
            break;

        ++bms;
        ++i;
    }

    return TRUE;
}

/*
 * Get the real path to a bookmark
 *
 * NULL is returned in case of no match, path resolution failure etc.
 * buf would be modified, so check return value before access
 */
static char *get_bm_loc(int key, char *buf)
{
    int r;
    ssize_t count;

    for (r = 0; bookmark[r].key && r < BM_MAX; ++r) {
        if (bookmark[r].key == key) {
            if (bookmark[r].loc[0] == '~') {
                char *home = getenv("HOME");

                if (!home) {
                    DPRINTF_S(messages[STR_NOHOME_ID]);
                    return NULL;
                }

                count = xstrlcpy(buf, home, PATH_MAX);
                xstrlcpy(buf + count - 1, bookmark[r].loc + 1, PATH_MAX - count - 1);
            } else
                xstrlcpy(buf, bookmark[r].loc, PATH_MAX);

            return buf;
        }
    }

    DPRINTF_S("Invalid key");
    return NULL;
}

static void resetdircolor(mode_t mode)
{
    if (cfg.dircolor && !S_ISDIR(mode)) {
        attroff(COLOR_PAIR(cfg.curctx + 1) | A_BOLD);
        cfg.dircolor = 0;
    }
}

/*
 * Replace escape characters in a string with '?'
 * Adjust string length to maxcols if > 0;
 *
 * Interestingly, note that unescape() uses g_buf. What happens if
 * str also points to g_buf? In this case we assume that the caller
 * acknowledges that it's OK to lose the data in g_buf after this
 * call to unescape().
 * The API, on its part, first converts str to multibyte (after which
 * it doesn't touch str anymore). Only after that it starts modifying
 * g_buf. This is a phased operation.
 */
static char *unescape(const char *str, uint maxcols)
{
    static wchar_t wbuf[PATH_MAX] __attribute__ ((aligned));
    static wchar_t *buf;
    static size_t len;

    /* Convert multi-byte to wide char */
    len = mbstowcs(wbuf, str, PATH_MAX);

    g_buf[0] = '\0';
    buf = wbuf;

    if (maxcols && len > maxcols) {
        len = wcswidth(wbuf, len);

        if (len > maxcols)
            wbuf[maxcols] = 0;
    }

    while (*buf) {
        if (*buf <= '\x1f' || *buf == '\x7f')
            *buf = '\?';

        ++buf;
    }

    /* Convert wide char to multi-byte */
    wcstombs(g_buf, wbuf, PATH_MAX);
    return g_buf;
}

static char *coolsize(off_t size)
{
    static const char * const U = "BKMGTPEZY";
    static char size_buf[12]; /* Buffer to hold human readable size */
    static off_t rem;
    static int i;

    i = 0;
    rem = 0;

    while (size > 1024) {
        rem = size & (0x3FF); /* 1024 - 1 = 0x3FF */
        size >>= 10;
        ++i;
    }

    if (i == 1) {
        rem = (rem * 1000) >> 10;

        rem /= 10;
        if (rem % 10 >= 5) {
            rem = (rem / 10) + 1;
            if (rem == 10) {
                ++size;
                rem = 0;
            }
        } else
            rem /= 10;
    } else if (i == 2) {
        rem = (rem * 1000) >> 10;

        if (rem % 10 >= 5) {
            rem = (rem / 10) + 1;
            if (rem == 100) {
                ++size;
                rem = 0;
            }
        } else
            rem /= 10;
    } else if (i > 0) {
        rem = (rem * 10000) >> 10;

        if (rem % 10 >= 5) {
            rem = (rem / 10) + 1;
            if (rem == 1000) {
                ++size;
                rem = 0;
            }
        } else
            rem /= 10;
    }

    if (i > 0 && i < 6)
        snprintf(size_buf, 12, "%lu.%0*lu%c", (ulong)size, i, (ulong)rem, U[i]);
    else
        snprintf(size_buf, 12, "%lu%c", (ulong)size, U[i]);

    return size_buf;
}

static char *get_file_sym(mode_t mode)
{
    static char ind[2] = "\0\0";

    switch (mode & S_IFMT) {
    case S_IFREG:
        if (mode & 0100)
            ind[0] = '*';
        break;
    case S_IFDIR:
        ind[0] = '/';
        break;
    case S_IFLNK:
        ind[0] = '@';
        break;
    case S_IFSOCK:
        ind[0] = '=';
        break;
    case S_IFIFO:
        ind[0] = '|';
        break;
    case S_IFBLK: // fallthrough
    case S_IFCHR:
        break;
    default:
        ind[0] = '?';
        break;
    }

    return ind;
}

static void printent(struct entry *ent, int sel, uint namecols)
{
    static char *pname;

    pname = unescape(ent->name, namecols);

    /* Directories are always shown on top */
    resetdircolor(ent->mode);

    printw("%s%s%s\n", CURSYM(sel), pname, get_file_sym(ent->mode));
}

static void printent_long(struct entry *ent, int sel, uint namecols)
{
    static char buf[18], *pname;

    strftime(buf, 18, "%F %R", localtime(&ent->t));
    pname = unescape(ent->name, namecols);

    /* Directories are always shown on top */
    resetdircolor(ent->mode);

    if (sel)
        attron(A_REVERSE);

    switch (ent->mode & S_IFMT) {
    case S_IFREG:
        if (ent->mode & 0100)
            printw(" %-16.16s %8.8s* %s*\n", buf,
                   coolsize(cfg.blkorder ? ent->blocks << BLK_SHIFT : ent->size), pname);
        else
            printw(" %-16.16s %8.8s  %s\n", buf,
                   coolsize(cfg.blkorder ? ent->blocks << BLK_SHIFT : ent->size), pname);
        break;
    case S_IFDIR:
        if (cfg.blkorder)
            printw(" %-16.16s %8.8s/ %s/\n",
                   buf, coolsize(ent->blocks << BLK_SHIFT), pname);
        else
            printw(" %-16.16s        /  %s/\n", buf, pname);
        break;
    case S_IFLNK:
        if (ent->flags & SYMLINK_TO_DIR)
            printw(" %-16.16s       @/  %s@\n", buf, pname);
        else
            printw(" %-16.16s        @  %s@\n", buf, pname);
        break;
    case S_IFSOCK:
        printw(" %-16.16s        =  %s=\n", buf, pname);
        break;
    case S_IFIFO:
        printw(" %-16.16s        |  %s|\n", buf, pname);
        break;
    case S_IFBLK:
        printw(" %-16.16s        b  %s\n", buf, pname);
        break;
    case S_IFCHR:
        printw(" %-16.16s        c  %s\n", buf, pname);
        break;
    default:
        printw(" %-16.16s %8.8s? %s?\n", buf,
               coolsize(cfg.blkorder ? ent->blocks << BLK_SHIFT : ent->size), pname);
        break;
    }

    if (sel)
        attroff(A_REVERSE);
}

static void (*printptr)(struct entry *ent, int sel, uint namecols) = &printent_long;

static char get_fileind(mode_t mode, char *desc)
{
    static char c;

    switch (mode & S_IFMT) {
    case S_IFREG:
        c = '-';
        xstrlcpy(desc, "regular file", DESCRIPTOR_LEN);
        if (mode & 0100)
            /* Length of string "regular file" is 12 */
            xstrlcpy(desc + 12, ", executable", DESCRIPTOR_LEN - 12);
        break;
    case S_IFDIR:
        c = 'd';
        xstrlcpy(desc, "directory", DESCRIPTOR_LEN);
        break;
    case S_IFLNK:
        c = 'l';
        xstrlcpy(desc, "symbolic link", DESCRIPTOR_LEN);
        break;
    case S_IFSOCK:
        c = 's';
        xstrlcpy(desc, "socket", DESCRIPTOR_LEN);
        break;
    case S_IFIFO:
        c = 'p';
        xstrlcpy(desc, "FIFO", DESCRIPTOR_LEN);
        break;
    case S_IFBLK:
        c = 'b';
        xstrlcpy(desc, "block special device", DESCRIPTOR_LEN);
        break;
    case S_IFCHR:
        c = 'c';
        xstrlcpy(desc, "character special device", DESCRIPTOR_LEN);
        break;
    default:
        /* Unknown type -- possibly a regular file? */
        c = '?';
        desc[0] = '\0';
        break;
    }

    return c;
}

/* Convert a mode field into "ls -l" type perms field. */
static char *get_lsperms(mode_t mode, char *desc)
{
    static const char * const rwx[] = {"---", "--x", "-w-", "-wx", "r--", "r-x", "rw-", "rwx"};
    static char bits[11] = {'\0'};

    bits[0] = get_fileind(mode, desc);
    xstrlcpy(&bits[1], rwx[(mode >> 6) & 7], 4);
    xstrlcpy(&bits[4], rwx[(mode >> 3) & 7], 4);
    xstrlcpy(&bits[7], rwx[(mode & 7)], 4);

    if (mode & S_ISUID)
        bits[3] = (mode & 0100) ? 's' : 'S';  /* user executable */
    if (mode & S_ISGID)
        bits[6] = (mode & 0010) ? 's' : 'l';  /* group executable */
    if (mode & S_ISVTX)
        bits[9] = (mode & 0001) ? 't' : 'T';  /* others executable */

    return bits;
}

/*
 * Gets only a single line (that's what we need
 * for now) or shows full command output in pager.
 *
 * If page is valid, returns NULL
 */
static char *get_output(char *buf, size_t bytes, char *file, char *arg1, char *arg2, bool page)
{
    pid_t pid;
    int pipefd[2];
    FILE *pf;
    int tmp, flags;
    char *ret = NULL;

    if (pipe(pipefd) == -1)
        errexit();

    for (tmp = 0; tmp < 2; ++tmp) {
        /* Get previous flags */
        flags = fcntl(pipefd[tmp], F_GETFL, 0);

        /* Set bit for non-blocking flag */
        flags |= O_NONBLOCK;

        /* Change flags on fd */
        fcntl(pipefd[tmp], F_SETFL, flags);
    }

    pid = fork();
    if (pid == 0) {
        /* In child */
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);
        execlp(file, file, arg1, arg2, NULL);
        _exit(1);
    }

    /* In parent */
    waitpid(pid, &tmp, 0);
    close(pipefd[1]);

    if (!page) {
        pf = fdopen(pipefd[0], "r");
        if (pf) {
            ret = fgets(buf, bytes, pf);
            close(pipefd[0]);
        }

        return ret;
    }


    pid = fork();
    if (pid == 0) {
        /* Show in pager in child */
        dup2(pipefd[0], STDIN_FILENO);
        close(pipefd[0]);
        execlp(pager, pager, NULL);
        _exit(1);
    }

    /* In parent */
    waitpid(pid, &tmp, 0);
    close(pipefd[0]);

    return NULL;
}

static bool getutil(char *util) {
    if (!get_output(g_buf, CMD_LEN_MAX, "which", util, NULL, FALSE))
        return FALSE;

    return TRUE;
}

static char *xgetpwuid(uid_t uid)
{
    struct passwd *pwd = getpwuid(uid);

    if (!pwd)
        return utils[UNKNOWN];

    return pwd->pw_name;
}

static char *xgetgrgid(gid_t gid)
{
    struct group *grp = getgrgid(gid);

    if (!grp)
        return utils[UNKNOWN];

    return grp->gr_name;
}

/*
 * Follows the stat(1) output closely
 */
static bool show_stats(char *fpath, char *fname, struct stat *sb)
{
    char desc[DESCRIPTOR_LEN];
    char *perms = get_lsperms(sb->st_mode, desc);
    char *p, *begin = g_buf;

    if (g_tmpfpath[0])
        xstrlcpy(g_tmpfpath + g_tmpfplen - 1, messages[STR_TMPFILE],
             HOME_LEN_MAX - g_tmpfplen);
    else {
        printmsg(messages[STR_NOHOME_ID]);
        return FALSE;
    }

    int fd = mkstemp(g_tmpfpath);

    if (fd == -1)
        return FALSE;

    dprintf(fd, "    File: '%s'", unescape(fname, 0));

    /* Show file name or 'symlink' -> 'target' */
    if (perms[0] == 'l') {
        /* Note that CMD_LEN_MAX > PATH_MAX */
        ssize_t len = readlink(fpath, g_buf, CMD_LEN_MAX);

        if (len != -1) {
            struct stat tgtsb;
            if (!stat(fpath, &tgtsb) && S_ISDIR(tgtsb.st_mode))
                g_buf[len++] = '/';

            g_buf[len] = '\0';

            /*
             * We pass g_buf but unescape() operates on g_buf too!
             * Read the API notes for information on how this works.
             */
            dprintf(fd, " -> '%s'", unescape(g_buf, 0));
        }
    }

    /* Show size, blocks, file type */
#if defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__APPLE__)
    dprintf(fd, "\n    Size: %-15lld Blocks: %-10lld IO Block: %-6d %s",
           (long long)sb->st_size, (long long)sb->st_blocks, sb->st_blksize, desc);
#else
    dprintf(fd, "\n    Size: %-15ld Blocks: %-10ld IO Block: %-6ld %s",
           sb->st_size, sb->st_blocks, (long)sb->st_blksize, desc);
#endif

    /* Show containing device, inode, hardlink count */
    snprintf(g_buf, 32, "%lxh/%lud", (ulong)sb->st_dev, (ulong)sb->st_dev);
#if defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__APPLE__)
    dprintf(fd, "\n  Device: %-15s Inode: %-11llu Links: %-9hu",
        g_buf, (unsigned long long)sb->st_ino, sb->st_nlink);
#else
    dprintf(fd, "\n  Device: %-15s Inode: %-11lu Links: %-9lu",
        g_buf, sb->st_ino, (ulong)sb->st_nlink);
#endif

    /* Show major, minor number for block or char device */
    if (perms[0] == 'b' || perms[0] == 'c')
        dprintf(fd, " Device type: %x,%x", major(sb->st_rdev), minor(sb->st_rdev));

    /* Show permissions, owner, group */
    dprintf(fd, "\n  Access: 0%d%d%d/%s Uid: (%u/%s)  Gid: (%u/%s)",
        (sb->st_mode >> 6) & 7, (sb->st_mode >> 3) & 7,
        sb->st_mode & 7, perms, sb->st_uid, xgetpwuid(sb->st_uid),
        sb->st_gid, xgetgrgid(sb->st_gid));

    /* Show last access time */
    strftime(g_buf, 40, messages[STR_DATE_ID], localtime(&sb->st_atime));
    dprintf(fd, "\n\n  Access: %s", g_buf);

    /* Show last modification time */
    strftime(g_buf, 40, messages[STR_DATE_ID], localtime(&sb->st_mtime));
    dprintf(fd, "\n  Modify: %s", g_buf);

    /* Show last status change time */
    strftime(g_buf, 40, messages[STR_DATE_ID], localtime(&sb->st_ctime));
    dprintf(fd, "\n  Change: %s", g_buf);

    if (S_ISREG(sb->st_mode)) {
        /* Show file(1) output */
        p = get_output(g_buf, CMD_LEN_MAX, "file", "-b", fpath, FALSE);
        if (p) {
            dprintf(fd, "\n\n ");
            while (*p) {
                if (*p == ',') {
                    *p = '\0';
                    dprintf(fd, " %s\n", begin);
                    begin = p + 1;
                }

                ++p;
            }
            dprintf(fd, " %s", begin);
        }

        dprintf(fd, "\n\n");
    } else
        dprintf(fd, "\n\n\n");

    close(fd);

    spawn(pager, pager_arg, g_tmpfpath, NULL, F_NORMAL);
    unlink(g_tmpfpath);
    return TRUE;
}

static size_t get_fs_info(const char *path, bool type)
{
    static struct statvfs svb;

    if (statvfs(path, &svb) == -1)
        return 0;

    if (type == CAPACITY)
        return svb.f_blocks << ffs(svb.f_bsize >> 1);

    return svb.f_bavail << ffs(svb.f_frsize >> 1);
}

static bool show_mediainfo(char *fpath, char *arg)
{
    if (!getutil(utils[cfg.metaviewer]))
        return FALSE;

    exitcurses();
    get_output(NULL, 0, utils[cfg.metaviewer], fpath, arg, TRUE);
    refresh();
    return TRUE;
}

static bool handle_archive(char *fpath, char *arg, char *dir)
{
    if (!getutil(utils[ATOOL]))
        return FALSE;

    if (arg[1] == 'x')
        spawn(utils[ATOOL], arg, fpath, dir, F_NORMAL);
    else {
        exitcurses();
        get_output(NULL, 0, utils[ATOOL], arg, fpath, TRUE);
        refresh();
    }

    return TRUE;
}

/*
 * The help string tokens (each line) start with a HEX value
 * which indicates the number of spaces to print before the
 * particular token. This method was chosen instead of a flat
 * string because the number of bytes in help was increasing
 * the binary size by around a hundred bytes. This would only
 * have increased as we keep adding new options.
 */
static bool show_help(char *path)
{
    if (g_tmpfpath[0])
        xstrlcpy(g_tmpfpath + g_tmpfplen - 1, messages[STR_TMPFILE],
             HOME_LEN_MAX - g_tmpfplen);
    else {
        printmsg(messages[STR_NOHOME_ID]);
        return FALSE;
    }

    int i = 0, fd = mkstemp(g_tmpfpath);
    char *start, *end;

    static char helpstr[] = {
"0\n"
"1NAVIGATION\n"
          "9↑, k  Up           PgUp, ^U  Scroll up\n"
          "9↓, j  Down         PgDn, ^D  Scroll down\n"
          "9←, h  Parent dir          ~  Go HOME\n"
       "6↵, →, l  Open file/dir       &  Start dir\n"
   "2Home, g, ^A  First entry         -  Last visited dir\n"
    "3End, G, ^E  Last entry          .  Toggle show hidden\n"
             "c/  Filter        Ins, ^T  Toggle nav-as-you-type\n"
             "cb  Pin current dir    ^W  Go to pinned dir\n"
       "6Tab, ^I  Next context        d  Toggle detail view\n"
         "8`, ^/  Leader key   N, LeadN  Go to/create context N\n"
           "aEsc  Exit prompt        ^L  Redraw/clear prompt\n"
            "b^G  Quit and cd         q  Quit context\n"
         "8Q, ^Q  Quit                ?  Help, config\n"
"1FILES\n"
            "b^O  Open with...        n  Create new/link\n"
             "cD  File details       ^R  Rename entry\n"
         "8⎵, ^K  Copy entry path     r  Open dir in vidir\n"
         "8Y, ^Y  Toggle selection    y  List selection\n"
             "cP  Copy selection      X  Delete selection\n"
             "cV  Move selection     ^X  Delete entry\n"
             "cf  Archive entry       F  List archive\n"
            "b^F  Extract archive  m, M  Brief/full media info\n"
             "ce  Edit in EDITOR      p  Open in PAGER\n"
"1ORDER TOGGLES\n"
            "b^J  Disk usage          S  Apparent du\n"
             "ct  Modification time   s  Size\n"
"1MISC\n"
         "8!, ^]  Spawn SHELL in dir  C  Execute entry\n"
         "8R, ^V  Run custom script   L  Lock terminal\n"
            "b^S  Run a command   N, ^N  Take note\n"};

    if (fd == -1)
        return FALSE;

    start = end = helpstr;
    while (*end) {
        while (*end != '\n')
            ++end;

        if (start == end) {
            ++end;
            continue;
        }

        dprintf(fd, "%*c%.*s", xchartohex(*start), ' ', (int)(end - start), start + 1);
        start = ++end;
    }

    dprintf(fd, "\nVOLUME: %s of ", coolsize(get_fs_info(path, FREE)));
    dprintf(fd, "%s free\n\n", coolsize(get_fs_info(path, CAPACITY)));

    if (getenv(env_cfg[NNN_BMS])) {
        dprintf(fd, "BOOKMARKS\n");
        for (; i < BM_MAX; ++i)
            if (bookmark[i].key)
                dprintf(fd, " %c: %s\n", (char)bookmark[i].key, bookmark[i].loc);
            else
                break;
        dprintf(fd, "\n");
    }

    for (i = NNN_OPENER; i <= NNN_TMPFILE; ++i) {
        start = getenv(env_cfg[i]);
        if (start)
            dprintf(fd, "%s: %s\n", env_cfg[i], start);
    }

    if (g_cppath[0])
        dprintf(fd, "COPY FILE: %s\n", g_cppath);

    for (i = NNN_USE_EDITOR; i <= NNN_PLAIN_FILTER; ++i) {
        if (getenv(env_cfg[i]))
            dprintf(fd, "%s: 1\n", env_cfg[i]);
    }

    dprintf(fd, "\n");

    start = getenv(envs[PWD]);
    if (start)
        dprintf(fd, "%s: %s\n", envs[PWD], start);
    if (getenv(envs[SHELL]))
        dprintf(fd, "%s: %s %s\n", envs[SHELL], shell, shell_arg);
    start = getenv(envs[SHLVL]);
    if (start)
        dprintf(fd, "%s: %s\n", envs[SHLVL], start);
    if (getenv(envs[VISUAL]))
        dprintf(fd, "%s: %s\n", envs[VISUAL], editor);
    else if (getenv(envs[EDITOR]))
        dprintf(fd, "%s: %s\n", envs[EDITOR], editor);
    if (getenv(envs[PAGER]))
        dprintf(fd, "%s: %s %s\n", envs[PAGER], pager, pager_arg);

    dprintf(fd, "\nv%s\n%s\n", VERSION, GENERAL_INFO);
    close(fd);

    spawn(pager, pager_arg, g_tmpfpath, NULL, F_NORMAL);
    unlink(g_tmpfpath);
    return TRUE;
}

static int sum_bsizes(const char *fpath, const struct stat *sb,
       int typeflag, struct FTW *ftwbuf)
{
    if (sb->st_blocks && (typeflag == FTW_F || typeflag == FTW_D))
        ent_blocks += sb->st_blocks;

    ++num_files;
    return 0;
}

static int sum_sizes(const char *fpath, const struct stat *sb,
       int typeflag, struct FTW *ftwbuf)
{
    if (sb->st_size && (typeflag == FTW_F || typeflag == FTW_D))
        ent_blocks += sb->st_size;

    ++num_files;
    return 0;
}

static void dentfree(struct entry *dents)
{
    free(pnamebuf);
    free(dents);
}

static int dentfill(char *path, struct entry **dents)
{
    static DIR *dirp;
    static struct dirent *dp;
    static char *namep, *pnb;
    static struct entry *dentp;
    static size_t off, namebuflen = NAMEBUF_INCR;
    static ulong num_saved;
    static int fd, n, count;
    static struct stat sb_path, sb;

    off = 0;

    dirp = opendir(path);
    if (dirp == NULL)
        return 0;

    fd = dirfd(dirp);

    n = 0;

    if (cfg.blkorder) {
        num_files = 0;
        dir_blocks = 0;

        if (fstatat(fd, ".", &sb_path, 0) == -1) {
            printwarn();
            return 0;
        }
    }

    while ((dp = readdir(dirp)) != NULL) {
        namep = dp->d_name;

        /* Skip self and parent */
        if ((namep[0] == '.' && (namep[1] == '\0' ||
            (namep[1] == '.' && namep[2] == '\0'))))
            continue;

        if (!cfg.showhidden && namep[0] == '.') {
            if (!cfg.blkorder)
                continue;

            if (fstatat(fd, namep, &sb, AT_SYMLINK_NOFOLLOW) == -1)
                continue;

            if (S_ISDIR(sb.st_mode)) {
                if (sb_path.st_dev == sb.st_dev) {
                    ent_blocks = 0;
                    mkpath(path, namep, g_buf);

                    if (nftw(g_buf, nftw_fn, open_max,
                         FTW_MOUNT | FTW_PHYS) == -1) {
                        printmsg(messages[STR_NFTWFAIL_ID]);
                        dir_blocks += (cfg.apparentsz
                                   ? sb.st_size
                                   : sb.st_blocks);
                    } else
                        dir_blocks += ent_blocks;
                }
            } else {
                dir_blocks += (cfg.apparentsz ? sb.st_size : sb.st_blocks);
                ++num_files;
            }

            continue;
        }

        if (fstatat(fd, namep, &sb, AT_SYMLINK_NOFOLLOW) == -1) {
            DPRINTF_S(namep);
            continue;
        }

        if (n == total_dents) {
            total_dents += ENTRY_INCR;
            *dents = xrealloc(*dents, total_dents * sizeof(**dents));
            if (*dents == NULL) {
                free(pnamebuf);
                errexit();
            }
            DPRINTF_P(*dents);
        }

        /* If not enough bytes left to copy a file name of length NAME_MAX, re-allocate */
        if (namebuflen - off < NAME_MAX + 1) {
            namebuflen += NAMEBUF_INCR;

            pnb = pnamebuf;
            pnamebuf = (char *)xrealloc(pnamebuf, namebuflen);
            if (pnamebuf == NULL) {
                free(*dents);
                errexit();
            }
            DPRINTF_P(pnamebuf);

            /* realloc() may result in memory move, we must re-adjust if that happens */
            if (pnb != pnamebuf) {
                dentp = *dents;
                dentp->name = pnamebuf;

                for (count = 1; count < n; ++dentp, ++count)
                    /* Current filename starts at last filename start + length */
                    (dentp + 1)->name = (char *)((size_t)dentp->name
                                + dentp->nlen);
            }
        }

        dentp = *dents + n;

        /* Copy file name */
        dentp->name = (char *)((size_t)pnamebuf + off);
        dentp->nlen = xstrlcpy(dentp->name, namep, NAME_MAX + 1);
        off += dentp->nlen;

        /* Copy other fields */
        dentp->mode = sb.st_mode;
        dentp->t = sb.st_mtime;
        dentp->size = sb.st_size;

        if (cfg.blkorder) {
            if (S_ISDIR(sb.st_mode)) {
                ent_blocks = 0;
                num_saved = num_files + 1;
                mkpath(path, namep, g_buf);

                if (nftw(g_buf, nftw_fn, open_max, FTW_MOUNT | FTW_PHYS) == -1) {
                    printmsg(messages[STR_NFTWFAIL_ID]);
                    dentp->blocks = (cfg.apparentsz ? sb.st_size : sb.st_blocks);
                } else
                    dentp->blocks = ent_blocks;

                if (sb_path.st_dev == sb.st_dev)
                    dir_blocks += dentp->blocks;
                else
                    num_files = num_saved;
            } else {
                dentp->blocks = (cfg.apparentsz ? sb.st_size : sb.st_blocks);
                dir_blocks += dentp->blocks;
                ++num_files;
            }
        }

        /* Flag if this is a symlink to a dir */
        if (S_ISLNK(sb.st_mode))
            if (!fstatat(fd, namep, &sb, 0)) {
                if (S_ISDIR(sb.st_mode))
                    dentp->flags |= SYMLINK_TO_DIR;
                else
                    dentp->flags &= ~SYMLINK_TO_DIR;
            }

        ++n;
    }

    /* Should never be null */
    if (closedir(dirp) == -1) {
        dentfree(*dents);
        errexit();
    }

    return n;
}

/*
 * Return the position of the matching entry or 0 otherwise
 * Note there's no NULL check for fname
 */
static int dentfind(const char *fname, int n)
{
    static int i;

    DPRINTF_S(fname);

    for (i = 0; i < n; ++i)
        if (xstrcmp(fname, dents[i].name) == 0)
            return i;

    return 0;
}

static void populate(char *path, char *lastname)
{
    if (cfg.blkorder) {
        printmsg("calculating...");
        refresh();
    }

#ifdef DEBUGMODE
    struct timespec ts1, ts2;

    clock_gettime(CLOCK_REALTIME, &ts1); /* Use CLOCK_MONOTONIC on FreeBSD */
#endif

    ndents = dentfill(path, &dents);
    if (!ndents)
        return;

    qsort(dents, ndents, sizeof(*dents), entrycmp);

#ifdef DEBUGMODE
    clock_gettime(CLOCK_REALTIME, &ts2);
    DPRINTF_U(ts2.tv_nsec - ts1.tv_nsec);
#endif

    /* Find cur from history */
    /* No NULL check for lastname, always points to an array */
    if (!*lastname)
        cur = 0;
    else
        cur = dentfind(lastname, ndents);
}

static void redraw(char *path)
{
    static char c;
    static char buf[12];
    static size_t ncols;
    static int nlines, i, attrs;
    static bool mode_changed;

    mode_changed = FALSE;
    nlines = MIN(LINES - 4, ndents);

    /* Clear screen */
    erase();

#ifdef DIR_LIMITED_COPY
    if (cfg.copymode)
        if (g_crc != crc8fast((uchar *)dents, ndents * sizeof(struct entry))) {
            cfg.copymode = 0;
            DPRINTF_S("selection off");
        }
#endif

    /* Fail redraw if < than 11 columns, context info prints 10 chars */
    if (COLS < 11) {
        printmsg("too few columns!");
        return;
    }

    /* Strip trailing slashes */
    for (i = strlen(path) - 1; i > 0; --i)
        if (path[i] == '/')
            path[i] = '\0';
        else
            break;

    DPRINTF_D(cur);
    DPRINTF_S(path);

    if (!realpath(path, g_buf)) {
        printwarn();
        return;
    }

    ncols = COLS;
    if (ncols > PATH_MAX)
        ncols = PATH_MAX;

    printw("[");
    for (i = 0; i < CTX_MAX; ++i) {
        /* Print current context in reverse */
        if (cfg.curctx == i) {
            if (cfg.showcolor)
                attrs = COLOR_PAIR(i + 1) | A_BOLD | A_REVERSE;
            else
                attrs = A_REVERSE;
            attron(attrs);
            printw("%d", i + 1);
            attroff(attrs);
            printw(" ");
        } else if (g_ctx[i].c_cfg.ctxactive) {
            if (cfg.showcolor)
                attrs = COLOR_PAIR(i + 1) | A_BOLD | A_UNDERLINE;
            else
                attrs = A_UNDERLINE;
            attron(attrs);
            printw("%d", i + 1);
            attroff(attrs);
            printw(" ");
        } else
            printw("%d ", i + 1);
    }
    printw("\b] "); /* 10 chars printed in total for contexts - "[1 2 3 4] " */

    attron(A_UNDERLINE);
    /* No text wrapping in cwd line */
    g_buf[ncols - 11] = '\0';
    printw("%s\n\n", g_buf);
    attroff(A_UNDERLINE);

    /* Fallback to light mode if less than 35 columns */
    if (ncols < 35 && cfg.showdetail) {
        cfg.showdetail ^= 1;
        printptr = &printent;
        mode_changed = TRUE;
    }

    /* Calculate the number of cols available to print entry name */
    if (cfg.showdetail)
        ncols -= 32;
    else
        ncols -= 5;

    if (cfg.showcolor) {
        attron(COLOR_PAIR(cfg.curctx + 1) | A_BOLD);
        cfg.dircolor = 1;
    }

    /* Print listing */
    if (cur < (nlines >> 1)) {
        for (i = 0; i < nlines; ++i)
            printptr(&dents[i], i == cur, ncols);
    } else if (cur >= ndents - (nlines >> 1)) {
        for (i = ndents - nlines; i < ndents; ++i)
            printptr(&dents[i], i == cur, ncols);
    } else {
        static int odd;

        odd = ISODD(nlines);
        nlines >>= 1;
        for (i = cur - nlines; i < cur + nlines + odd; ++i)
            printptr(&dents[i], i == cur, ncols);
    }

    /* Must reset e.g. no files in dir */
    if (cfg.dircolor) {
        attroff(COLOR_PAIR(cfg.curctx + 1) | A_BOLD);
        cfg.dircolor = 0;
    }

    if (cfg.showdetail) {
        if (ndents) {
            static char sort[9];

            if (cfg.mtimeorder)
                xstrlcpy(sort, "by time ", 9);
            else if (cfg.sizeorder)
                xstrlcpy(sort, "by size ", 9);
            else
                sort[0] = '\0';

            /* We need to show filename as it may be truncated in directory listing */
            if (!cfg.blkorder)
                mvprintw(LINES - 1, 0, "%d/%d %s[%s%s]\n", cur + 1, ndents, sort,
                     unescape(dents[cur].name, NAME_MAX),
                     get_file_sym(dents[cur].mode));
            else {
                xstrlcpy(buf, coolsize(dir_blocks << BLK_SHIFT), 12);
                if (cfg.apparentsz)
                    c = 'a';
                else
                    c = 'd';

                mvprintw(LINES - 1, 0,
                     "%d/%d %cu: %s (%lu files) vol: %s free [%s%s]\n",
                     cur + 1, ndents, c, buf, num_files,
                     coolsize(get_fs_info(path, FREE)),
                     unescape(dents[cur].name, NAME_MAX),
                     get_file_sym(dents[cur].mode));
            }
        } else
            printmsg("0 items");
    }

    if (mode_changed) {
        cfg.showdetail ^= 1;
        printptr = &printent_long;
    }
}

static void browse(char *ipath)
{
    char newpath[PATH_MAX] __attribute__ ((aligned));
    char mark[PATH_MAX] __attribute__ ((aligned));
    char rundir[PATH_MAX] __attribute__ ((aligned));
    char runfile[NAME_MAX + 1] __attribute__ ((aligned));
    char *path, *lastdir, *lastname;
    char *dir, *tmp;
    struct stat sb;
    int r = -1, fd, presel, ncp = 0, copystartid = 0, copyendid = 0;
    enum action sel;
    bool dir_changed = FALSE;

    /* setup first context */
    xstrlcpy(g_ctx[0].c_path, ipath, PATH_MAX); /* current directory */
    path = g_ctx[0].c_path;
    xstrlcpy(g_ctx[0].c_init, ipath, PATH_MAX); /* start directory */
    g_ctx[0].c_last[0] = g_ctx[0].c_name[0] = newpath[0] = mark[0] = '\0';
    rundir[0] = runfile[0] = '\0';
    lastdir = g_ctx[0].c_last; /* last visited directory */
    lastname = g_ctx[0].c_name; /* last visited filename */
    g_ctx[0].c_cfg = cfg; /* current configuration */

    if (cfg.filtermode)
        presel = FILTER;
    else
        presel = 0;

    dents = xrealloc(dents, total_dents * sizeof(struct entry));
    if (dents == NULL)
        errexit();
    DPRINTF_P(dents);

    /* Allocate buffer to hold names */
    pnamebuf = (char *)xrealloc(pnamebuf, NAMEBUF_INCR);
    if (pnamebuf == NULL) {
        free(dents);
        errexit();
    }
    DPRINTF_P(pnamebuf);

begin:
#ifdef LINUX_INOTIFY
    if ((presel == FILTER || dir_changed) && inotify_wd >= 0) {
        inotify_rm_watch(inotify_fd, inotify_wd);
        inotify_wd = -1;
        dir_changed = FALSE;
    }
#elif defined(BSD_KQUEUE)
    if ((presel == FILTER || dir_changed) && event_fd >= 0) {
        close(event_fd);
        event_fd = -1;
        dir_changed = FALSE;
    }
#endif

    /* Can fail when permissions change while browsing.
     * It's assumed that path IS a directory when we are here.
     */
    if (access(path, R_OK) == -1) {
        printwarn();
        goto nochange;
    }

    populate(path, lastname);

#ifdef LINUX_INOTIFY
    if (inotify_wd == -1)
        inotify_wd = inotify_add_watch(inotify_fd, path, INOTIFY_MASK);
#elif defined(BSD_KQUEUE)
    if (event_fd == -1) {
#if defined(O_EVTONLY)
        event_fd = open(path, O_EVTONLY);
#else
        event_fd = open(path, O_RDONLY);
#endif
        if (event_fd >= 0)
            EV_SET(&events_to_monitor[0], event_fd, EVFILT_VNODE,
                   EV_ADD | EV_CLEAR, KQUEUE_FFLAGS, 0, path);
    }
#endif

    while (1) {
        redraw(path);
nochange:
        /* Exit if parent has exited */
        if (getppid() == 1)
            _exit(0);

        sel = nextsel(&presel);

        switch (sel) {
        case SEL_BACK:
            /* There is no going back */
            if (istopdir(path)) {
                /* Continue in navigate-as-you-type mode, if enabled */
                if (cfg.filtermode)
                    presel = FILTER;
                goto nochange;
            }

            dir = xdirname(path);
            if (access(dir, R_OK) == -1) {
                printwarn();
                goto nochange;
            }

            /* Save history */
            xstrlcpy(lastname, xbasename(path), NAME_MAX + 1);

            /* Save last working directory */
            xstrlcpy(lastdir, path, PATH_MAX);

            xstrlcpy(path, dir, PATH_MAX);

            setdirwatch();
            goto begin;
        case SEL_NAV_IN: // fallthrough
        case SEL_GOIN:
            /* Cannot descend in empty directories */
            if (!ndents)
                goto begin;

            mkpath(path, dents[cur].name, newpath);
            DPRINTF_S(newpath);

            /* Cannot use stale data in entry, file may be missing by now */
            if (stat(newpath, &sb) == -1) {
                printwarn();
                goto nochange;
            }
            DPRINTF_U(sb.st_mode);

            switch (sb.st_mode & S_IFMT) {
            case S_IFDIR:
                if (access(newpath, R_OK) == -1) {
                    printwarn();
                    goto nochange;
                }

                /* Save last working directory */
                xstrlcpy(lastdir, path, PATH_MAX);

                xstrlcpy(path, newpath, PATH_MAX);
                lastname[0] = '\0';
                setdirwatch();
                goto begin;
            case S_IFREG:
            {
                /* If opened as vim plugin and Enter/^M pressed, pick */
                if (cfg.picker && sel == SEL_GOIN) {
                    r = mkpath(path, dents[cur].name, newpath);
                    appendfpath(newpath, r);
                    writecp(pcopybuf, copybufpos - 1);

                    dentfree(dents);
                    return;
                }

                /* If open file is disabled on right arrow or `l`, return */
                if (cfg.nonavopen && sel == SEL_NAV_IN)
                    continue;

                /* Handle script selection mode */
                if (cfg.runscript) {
                    if (cfg.runctx != cfg.curctx)
                        continue;

                    /* Must be in script directory to select script */
                    if (strcmp(path, scriptpath) != 0)
                        continue;

                    mkpath(path, dents[cur].name, newpath);
                    xstrlcpy(path, rundir, PATH_MAX);
                    if (runfile[0]) {
                        xstrlcpy(lastname, runfile, NAME_MAX);
                        spawn(shell, newpath, lastname, path,
                              F_NORMAL | F_SIGINT);
                        runfile[0] = '\0';
                    } else
                        spawn(shell, newpath, NULL, path, F_NORMAL | F_SIGINT);
                    rundir[0] = '\0';
                    cfg.runscript = 0;
                    setdirwatch();
                    goto begin;
                }

                /* If NNN_USE_EDITOR is set, open text in EDITOR */
                if (cfg.useeditor &&
                    get_output(g_buf, CMD_LEN_MAX, "file", FILE_OPTS, newpath, FALSE)
                    && g_buf[0] == 't' && g_buf[1] == 'e' && g_buf[2] == 'x'
                    && g_buf[3] == g_buf[0] && g_buf[4] == '/') {
                    if (!quote_run_sh_cmd(editor, newpath, path))
                        goto nochange;
                    continue;
                }

                if (!sb.st_size && cfg.restrict0b) {
                    printmsg("empty: use edit or open with");
                    goto nochange;
                }

                /* Invoke desktop opener as last resort */
                spawn(opener, newpath, NULL, NULL, F_NOWAIT | F_NOTRACE);
                continue;
            }
            default:
                printmsg("unsupported file");
                goto nochange;
            }
        case SEL_NEXT: // fallthrough
        case SEL_PREV: // fallthrough
        case SEL_PGDN: // fallthrough
        case SEL_PGUP: // fallthrough
        case SEL_HOME: // fallthrough
        case SEL_END:
            switch (sel) {
            case SEL_NEXT:
                if (cur < ndents - 1)
                    ++cur;
                else if (ndents)
                    /* Roll over, set cursor to first entry */
                    cur = 0;
                break;
            case SEL_PREV:
                if (cur > 0)
                    --cur;
                else if (ndents)
                    /* Roll over, set cursor to last entry */
                    cur = ndents - 1;
                break;
            case SEL_PGDN:
                if (cur < ndents - 1)
                    cur += MIN((LINES - 4) / 2, ndents - 1 - cur);
                break;
            case SEL_PGUP:
                if (cur > 0)
                    cur -= MIN((LINES - 4) / 2, cur);
                break;
            case SEL_HOME:
                cur = 0;
                break;
            default: /* case SEL_END */
                cur = ndents - 1;
                break;
            }
            break;
        case SEL_CDHOME: // fallthrough
        case SEL_CDBEGIN: // fallthrough
        case SEL_CDLAST: // fallthrough
        case SEL_VISIT:
            switch (sel) {
            case SEL_CDHOME:
                dir = xgetenv("HOME", path);
                break;
            case SEL_CDBEGIN:
                dir = ipath;
                break;
            case SEL_CDLAST:
                dir = lastdir;
                break;
            default: /* case SEL_VISIT */
                dir = mark;
                break;
            }

            if (dir[0] == '\0') {
                printmsg("not set");
                goto nochange;
            }

            if (!xdiraccess(dir))
                goto nochange;

            if (strcmp(path, dir) == 0)
                break;

            /* SEL_CDLAST: dir pointing to lastdir */
            xstrlcpy(newpath, dir, PATH_MAX);

            /* Save last working directory */
            xstrlcpy(lastdir, path, PATH_MAX);

            xstrlcpy(path, newpath, PATH_MAX);
            lastname[0] = '\0';
            DPRINTF_S(path);
            setdirwatch();
            goto begin;
        case SEL_LEADER: // fallthrough
        case SEL_CYCLE: // fallthrough
        case SEL_CTX1: // fallthrough
        case SEL_CTX2: // fallthrough
        case SEL_CTX3: // fallthrough
        case SEL_CTX4:
            if (sel == SEL_CYCLE)
                fd = '>';
            else if (sel >= SEL_CTX1 && sel <= SEL_CTX4)
                fd = sel - SEL_CTX1 + '1';
            else
                fd = get_input(NULL);

            switch (fd) {
            case 'q': // fallthrough
            case '~': // fallthrough
            case '-': // fallthrough
            case '&':
                presel = fd;
                goto nochange;
            case '>': // fallthrough
            case '.': // fallthrough
            case '<': // fallthrough
            case ',':
                r = cfg.curctx;
                if (fd == '>' || fd == '.')
                    do
                        (r == CTX_MAX - 1) ? (r = 0) : ++r;
                    while (!g_ctx[r].c_cfg.ctxactive);
                else
                    do
                        (r == 0) ? (r = CTX_MAX - 1) : --r;
                    while (!g_ctx[r].c_cfg.ctxactive); // fallthrough
                fd = '1' + r; // fallthrough
            case '1': // fallthrough
            case '2': // fallthrough
            case '3': // fallthrough
            case '4':
                r = fd - '1'; /* Save the next context id */
                if (cfg.curctx == r) {
                    if (sel == SEL_CYCLE) {
                        (r == CTX_MAX - 1) ? (r = 0) : ++r;
                        snprintf(newpath, PATH_MAX,
                             "Create context %d?  (Enter)", r + 1);
                        fd = get_input(newpath);
                        if (fd != '\r')
                            continue;
                    } else
                        continue;
                }

#ifdef DIR_LIMITED_COPY
                g_crc = 0;
#endif

                /* Save current context */
                xstrlcpy(g_ctx[cfg.curctx].c_name, dents[cur].name, NAME_MAX + 1);
                g_ctx[cfg.curctx].c_cfg = cfg;

                if (g_ctx[r].c_cfg.ctxactive) /* Switch to saved context */
                    cfg = g_ctx[r].c_cfg;
                else { /* Setup a new context from current context */
                    g_ctx[r].c_cfg.ctxactive = 1;
                    xstrlcpy(g_ctx[r].c_path, path, PATH_MAX);
                    xstrlcpy(g_ctx[r].c_init, path, PATH_MAX);
                    g_ctx[r].c_last[0] = '\0';
                    xstrlcpy(g_ctx[r].c_name, dents[cur].name, NAME_MAX + 1);
                    g_ctx[r].c_cfg = cfg;
                    g_ctx[r].c_cfg.runscript = 0;
                }

                /* Reset the pointers */
                path = g_ctx[r].c_path;
                ipath = g_ctx[r].c_init;
                lastdir = g_ctx[r].c_last;
                lastname = g_ctx[r].c_name;

                cfg.curctx = r;
                setdirwatch();
                goto begin;
            }

            if (get_bm_loc(fd, newpath) == NULL) {
                printmsg(messages[STR_INVBM_KEY]);
                goto nochange;
            }

            if (!xdiraccess(newpath))
                goto nochange;

            if (strcmp(path, newpath) == 0)
                break;

            lastname[0] = '\0';

            /* Save last working directory */
            xstrlcpy(lastdir, path, PATH_MAX);

            /* Save the newly opted dir in path */
            xstrlcpy(path, newpath, PATH_MAX);
            DPRINTF_S(path);

            setdirwatch();
            goto begin;
        case SEL_PIN:
            xstrlcpy(mark, path, PATH_MAX);
            printmsg(mark);
            goto nochange;
        case SEL_FLTR:
            presel = filterentries(path);
            /* Save current */
            if (ndents)
                copycurname();
            goto nochange;
        case SEL_MFLTR: // fallthrough
        case SEL_TOGGLEDOT: // fallthrough
        case SEL_DETAIL: // fallthrough
        case SEL_FSIZE: // fallthrough
        case SEL_BSIZE: // fallthrough
        case SEL_MTIME:
            switch (sel) {
            case SEL_MFLTR:
                cfg.filtermode ^= 1;
                if (cfg.filtermode) {
                    presel = FILTER;
                    goto nochange;
                }

                /* Start watching the directory */
                dir_changed = TRUE;
                break;
            case SEL_TOGGLEDOT:
                cfg.showhidden ^= 1;
                break;
            case SEL_DETAIL:
                cfg.showdetail ^= 1;
                cfg.showdetail ? (printptr = &printent_long) : (printptr = &printent);
                break;
            case SEL_FSIZE:
                cfg.sizeorder ^= 1;
                cfg.mtimeorder = 0;
                cfg.apparentsz = 0;
                cfg.blkorder = 0;
                cfg.copymode = 0;
                break;
            case SEL_BSIZE:
                if (sel == SEL_BSIZE) {
                    if (!cfg.apparentsz)
                        cfg.blkorder ^= 1;
                    nftw_fn = &sum_bsizes;
                    cfg.apparentsz = 0;
                    BLK_SHIFT = ffs(S_BLKSIZE) - 1;
                }

                if (cfg.blkorder) {
                    cfg.showdetail = 1;
                    printptr = &printent_long;
                }
                cfg.mtimeorder = 0;
                cfg.sizeorder = 0;
                cfg.copymode = 0;
                break;
            default: /* SEL_MTIME */
                cfg.mtimeorder ^= 1;
                cfg.sizeorder = 0;
                cfg.apparentsz = 0;
                cfg.blkorder = 0;
                cfg.copymode = 0;
                break;
            }

            /* Save current */
            if (ndents)
                copycurname();
            goto begin;
        case SEL_STATS:
            if (!ndents)
                break;

            mkpath(path, dents[cur].name, newpath);
            if (lstat(newpath, &sb) == -1 || !show_stats(newpath, dents[cur].name, &sb)) {
                printwarn();
                goto nochange;
            }
            break;
        case SEL_MEDIA: // fallthrough
        case SEL_FMEDIA: // fallthrough
        case SEL_ARCHIVELS: // fallthrough
        case SEL_EXTRACT: // fallthrough
        case SEL_RENAMEALL: // fallthrough
        case SEL_RUNEDIT: // fallthrough
        case SEL_RUNPAGE:
            if (!ndents)
                break; // fallthrough
        case SEL_REDRAW: // fallthrough
        case SEL_HELP: // fallthrough
        case SEL_NOTE: // fallthrough
        case SEL_LOCK:
        {
            if (ndents)
                mkpath(path, dents[cur].name, newpath);

            switch (sel) {
            case SEL_MEDIA:
                r = show_mediainfo(newpath, NULL);
                break;
            case SEL_FMEDIA:
                r = show_mediainfo(newpath, "-f");
                break;
            case SEL_ARCHIVELS:
                r = handle_archive(newpath, "-l", path);
                break;
            case SEL_EXTRACT:
                r = handle_archive(newpath, "-x", path);
                break;
            case SEL_REDRAW:
                if (ndents)
                    copycurname();
                goto begin;
            case SEL_RENAMEALL:
                if ((r = getutil(utils[VIDIR])))
                    spawn(utils[VIDIR], ".", NULL, path, F_NORMAL);
                break;
            case SEL_HELP:
                r = show_help(path);
                break;
            case SEL_RUNEDIT:
                if (!quote_run_sh_cmd(editor, dents[cur].name, path))
                    goto nochange;
                r = TRUE;
                break;
            case SEL_RUNPAGE:
                r = TRUE;
                spawn(pager, pager_arg, dents[cur].name, path, F_NORMAL);
                break;
            case SEL_NOTE:
                tmp = getenv(env_cfg[NNN_NOTE]);
                if (!tmp) {
                    printmsg("set NNN_NOTE");
                    goto nochange;
                }

                if (!quote_run_sh_cmd(editor, tmp, NULL))
                    goto nochange;
                r = TRUE;
                break;
            default: /* SEL_LOCK */
                r = TRUE;
                spawn(utils[LOCKER], NULL, NULL, NULL, F_NORMAL | F_SIGINT);
                break;
            }

            if (!r) {
                printmsg("required utility missing");
                goto nochange;
            }

            /* In case of successful operation, reload contents */

            /* Continue in navigate-as-you-type mode, if enabled */
            if (cfg.filtermode)
                presel = FILTER;

            /* Save current */
            if (ndents)
                copycurname();

            /* Repopulate as directory content may have changed */
            goto begin;
        }
        case SEL_ASIZE:
            cfg.apparentsz ^= 1;
            if (cfg.apparentsz) {
                nftw_fn = &sum_sizes;
                cfg.blkorder = 1;
                BLK_SHIFT = 0;
            } else
                cfg.blkorder = 0; // fallthrough
        case SEL_COPY:
            if (!ndents)
                goto nochange;

            if (cfg.copymode) {
                /*
                 * Clear the tmp copy path file on first copy.
                 *
                 * This ensures that when the first file path is
                 * copied into memory (but not written to tmp file
                 * yet to save on writes), the tmp file is cleared.
                 * The user may be in the middle of selection mode op
                 * and issue a cp, mv of multi-rm assuming the files
                 * in the copy list would be affected. However, these
                 * ops read the source file paths from the tmp file.
                 */
                if (!ncp)
                    writecp(NULL, 0);

                r = mkpath(path, dents[cur].name, newpath);
                if (!appendfpath(newpath, r))
                    goto nochange;

                ++ncp;
            } else {
                r = mkpath(path, dents[cur].name, newpath);
                /* Keep the copy buf in sync */
                copybufpos = 0;
                appendfpath(newpath, r);

                writecp(newpath, r - 1); /* Truncate NULL from end */
                if (copier)
                    spawn(copier, NULL, NULL, NULL, F_NOTRACE);
            }
            printmsg(newpath);
            goto nochange;
        case SEL_COPYMUL:
            cfg.copymode ^= 1;
            if (cfg.copymode) {
                g_crc = crc8fast((uchar *)dents, ndents * sizeof(struct entry));
                copystartid = cur;
                copybufpos = 0;
                ncp = 0;
                printmsg("selection on");
                DPRINTF_S("selection on");
                goto nochange;
            }

            if (!ncp) { /* Handle range selection */
#ifndef DIR_LIMITED_COPY
                if (g_crc != crc8fast((uchar *)dents,
                              ndents * sizeof(struct entry))) {
                    cfg.copymode = 0;
                    printmsg("range error: dir/content changed");
                    DPRINTF_S("range error: dir/content changed");
                    goto nochange;
                }
#endif
                if (cur < copystartid) {
                    copyendid = copystartid;
                    copystartid = cur;
                } else
                    copyendid = cur;

                if (copystartid < copyendid) {
                    for (r = copystartid; r <= copyendid; ++r)
                        if (!appendfpath(newpath, mkpath(path,
                                 dents[r].name, newpath)))
                            goto nochange;

                    mvprintw(LINES - 1, 0, "%d files copied\n",
                         copyendid - copystartid + 1);
                }
            }

            if (copybufpos) { /* File path(s) written to the buffer */
                writecp(pcopybuf, copybufpos - 1); /* Truncate NULL from end */
                if (copier)
                    spawn(copier, NULL, NULL, NULL, F_NOTRACE);

                if (ncp) /* Some files cherry picked */
                    mvprintw(LINES - 1, 0, "%d files copied\n", ncp);
            } else
                printmsg("selection off");
            goto nochange;
        case SEL_COPYLIST:
            if (copybufpos)
                showcplist();
            else
                printmsg("none selected");
            goto nochange;
        case SEL_CP:
        case SEL_MV:
        case SEL_RMMUL:
        {
            /* Fail if copy file path not generated */
            if (!g_cppath[0]) {
                printmsg("copy file not found");
                goto nochange;
            }

            /* Warn if selection not completed */
            if (cfg.copymode) {
                printmsg("finish selection first");
                goto nochange;
            }

            /* Fail if copy file path isn't accessible */
            if (access(g_cppath, R_OK) == -1) {
                printmsg("empty selection list");
                goto nochange;
            }

            if (sel == SEL_CP) {
                snprintf(g_buf, CMD_LEN_MAX,
#ifdef __linux__
                     "xargs -0 -a %s -%c src cp -iRp src .",
#else
                     "cat %s | xargs -0 -o -%c src cp -iRp src .",
#endif
                     g_cppath, REPLACE_STR);
            } else if (sel == SEL_MV) {
                snprintf(g_buf, CMD_LEN_MAX,
#ifdef __linux__
                     "xargs -0 -a %s -%c src mv -i src .",
#else
                     "cat %s | xargs -0 -o -%c src mv -i src .",
#endif
                     g_cppath, REPLACE_STR);
            } else { /* SEL_RMMUL */
                snprintf(g_buf, CMD_LEN_MAX,
#ifdef __linux__
                     "xargs -0 -a %s rm -%cr",
#else
                     "cat %s | xargs -0 -o rm -%cr",
#endif
                     g_cppath, confirm_force());
            }

            spawn("sh", "-c", g_buf, path, F_NORMAL | F_SIGINT);

            if (ndents)
                copycurname();
            if (cfg.filtermode)
                presel = FILTER;
            goto begin;
        }
        case SEL_RM:
        {
            if (!ndents)
                break;

            char rm_opts[] = "-ir";
            rm_opts[1] = confirm_force();

            mkpath(path, dents[cur].name, newpath);
            spawn("rm", rm_opts, newpath, NULL, F_NORMAL | F_SIGINT);

            if (cur && access(newpath, F_OK) == -1)
                --cur;

            copycurname();

            if (cfg.filtermode)
                presel = FILTER;
            goto begin;
        }
        case SEL_ARCHIVE: // fallthrough
        case SEL_OPENWITH: // fallthrough
        case SEL_RENAME:
            if (!ndents)
                break; // fallthrough
        case SEL_NEW:
        {
            switch (sel) {
            case SEL_ARCHIVE:
                tmp = xreadline(dents[cur].name, "name: ");
                break;
            case SEL_OPENWITH:
                tmp = xreadline(NULL, "open with: ");
                break;
            case SEL_NEW:
                tmp = xreadline(NULL, "name/link suffix [@ for none]: ");
                break;
            default: /* SEL_RENAME */
                tmp = xreadline(dents[cur].name, "");
                break;
            }

            if (tmp == NULL || tmp[0] == '\0')
                break;

            /* Allow only relative, same dir paths */
            if (tmp[0] == '/' || xstrcmp(xbasename(tmp), tmp) != 0) {
                printmsg(messages[STR_INPUT_ID]);
                goto nochange;
            }

            /* Confirm if app is CLI or GUI */
            if (sel == SEL_OPENWITH) {
                r = get_input("press 'c' for cli mode");
                (r == 'c') ? (r = F_NORMAL) : (r = F_NOWAIT | F_NOTRACE);
            }

            switch (sel) {
            case SEL_ARCHIVE:
                /* newpath is used as temporary buffer */
                if (!getutil(utils[APACK])) {
                    printmsg("utility missing");
                    continue;
                }

                spawn(utils[APACK], tmp, dents[cur].name, path, F_NORMAL);
                break;
            case SEL_OPENWITH:
                dir = NULL;
                getprogarg(tmp, &dir); /* dir used as tmp var */
                mkpath(path, dents[cur].name, newpath);
                spawn(tmp, dir, newpath, path, r);
                break;
            case SEL_RENAME:
                /* Skip renaming to same name */
                if (strcmp(tmp, dents[cur].name) == 0)
                    goto nochange;
                break;
            default:
                break;
            }

            /* Complete OPEN, LAUNCH, ARCHIVE operations */
            if (sel != SEL_NEW && sel != SEL_RENAME) {
                /* Continue in navigate-as-you-type mode, if enabled */
                if (cfg.filtermode)
                    presel = FILTER;

                /* Save current */
                copycurname();

                /* Repopulate as directory content may have changed */
                goto begin;
            }

            /* Open the descriptor to currently open directory */
            fd = open(path, O_RDONLY | O_DIRECTORY);
            if (fd == -1) {
                printwarn();
                goto nochange;
            }

            /* Check if another file with same name exists */
            if (faccessat(fd, tmp, F_OK, AT_SYMLINK_NOFOLLOW) != -1) {
                if (sel == SEL_RENAME) {
                    /* Overwrite file with same name? */
                    if (get_input("press 'y' to overwrite") != 'y') {
                        close(fd);
                        break;
                    }
                } else {
                    /* Do nothing in case of NEW */
                    close(fd);
                    printmsg("entry exists");
                    goto nochange;
                }
            }

            if (sel == SEL_RENAME) {
                /* Rename the file */
                if (renameat(fd, dents[cur].name, fd, tmp) != 0) {
                    close(fd);
                    printwarn();
                    goto nochange;
                }
            } else {
                /* Check if it's a dir or file */
                r = get_input("create 'f'(ile) / 'd'(ir) / 's'(ym) / 'h'(ard)?");
                if (r == 'f') {
                    r = openat(fd, tmp, O_CREAT, 0666);
                    close(r);
                } else if (r == 'd') {
                    r = mkdirat(fd, tmp, 0777);
                } else if (r == 's' || r == 'h') {
                    if (tmp[0] == '@' && tmp[1] == '\0')
                        tmp[0] = '\0';
                    r = xlink(tmp, path, newpath, r);
                    close(fd);

                    if (r <= 0) {
                        printmsg("none created");
                        goto nochange;
                    }

                    if (cfg.filtermode)
                        presel = FILTER;
                    if (ndents)
                        copycurname();
                    goto begin;
                } else {
                    close(fd);
                    break;
                }

                /* Check if file creation failed */
                if (r == -1) {
                    close(fd);
                    printwarn();
                    goto nochange;
                }
            }

            close(fd);
            xstrlcpy(lastname, tmp, NAME_MAX + 1);
            goto begin;
        }
        case SEL_EXEC: // fallthrough
        case SEL_SHELL: // fallthrough
        case SEL_SCRIPT: // fallthrough
        case SEL_RUNCMD:
            switch (sel) {
            case SEL_EXEC:
                if (!ndents)
                    goto nochange;

                /* Check if this is a directory */
                if (!S_ISREG(dents[cur].mode)) {
                    printmsg("not regular file");
                    goto nochange;
                }

                /* Check if file is executable */
                if (!(dents[cur].mode & 0100)) {
                    printmsg("permission denied");
                    goto nochange;
                }

                mkpath(path, dents[cur].name, newpath);
                DPRINTF_S(newpath);
                spawn(newpath, NULL, NULL, path, F_NORMAL | F_SIGINT);
                break;
            case SEL_SHELL:
                spawn(shell, shell_arg, NULL, path, F_NORMAL | F_MARKER);
                break;
            case SEL_SCRIPT:
                if (!scriptpath) {
                    printmsg("set NNN_SCRIPT");
                    goto nochange;
                }

                if (stat(scriptpath, &sb) == -1) {
                    printwarn();
                    goto nochange;
                }

                if (S_ISDIR(sb.st_mode)) {
                    cfg.runscript ^= 1;
                    if (!cfg.runscript && rundir[0]) {
                        /* If toggled, and still in the script dir,
                           switch to original directory */
                        if (strcmp(path, scriptpath) == 0) {
                            xstrlcpy(path, rundir, PATH_MAX);
                            xstrlcpy(lastname, runfile, NAME_MAX);
                            rundir[0] = runfile[0] = '\0';
                            setdirwatch();
                            goto begin;
                        }
                        break;
                    }

                    /* Check if directory is accessible */
                    if (!xdiraccess(scriptpath))
                        goto nochange;

                    xstrlcpy(rundir, path, PATH_MAX);
                    xstrlcpy(path, scriptpath, PATH_MAX);
                    if (ndents)
                        xstrlcpy(runfile, dents[cur].name, NAME_MAX);
                    cfg.runctx = cfg.curctx;
                    lastname[0] = '\0';
                    setdirwatch();
                    goto begin;
                }

                if (S_ISREG(sb.st_mode)) {
                    if (ndents)
                        tmp = dents[cur].name;
                    else
                        tmp = NULL;
                    spawn(shell, scriptpath, tmp, path, F_NORMAL | F_SIGINT);
                }
                break;
            default: /* SEL_RUNCMD */
                tmp = xreadline(NULL, "> ");
                if (tmp && tmp[0])
                    spawn(shell, "-c", tmp, path, F_NORMAL | F_SIGINT);
            }

            /* Continue in navigate-as-you-type mode, if enabled */
            if (cfg.filtermode)
                presel = FILTER;

            /* Save current */
            if (ndents)
                copycurname();

            /* Repopulate as directory content may have changed */
            goto begin;
        case SEL_QUITCD: // fallthrough
        case SEL_QUIT:
            for (r = 0; r < CTX_MAX; ++r)
                if (r != cfg.curctx && g_ctx[r].c_cfg.ctxactive) {
                    r = get_input("Quit all contexts? ('Enter' confirms)");
                    break;
                }

            if (!(r == CTX_MAX || r == '\r'))
                break;

            if (sel == SEL_QUITCD) {
                /* In vim picker mode, clear selection and exit */
                if (cfg.picker) {
                    if (copybufpos) {
                        if (cfg.pickraw) /* Reset for for raw pick */
                            copybufpos = 0;
                        else /* Clear the picker file */
                            writecp(NULL, 0);
                    }
                    dentfree(dents);
                    return;
                }

                tmp = getenv(env_cfg[NNN_TMPFILE]);
                if (!tmp) {
                    printmsg("set NNN_TMPFILE");
                    goto nochange;
                }

                FILE *fp = fopen(tmp, "w");

                if (fp) {
                    fprintf(fp, "cd \"%s\"", path);
                    fclose(fp);
                }
            } // fallthrough
        case SEL_QUITCTX:
            if (sel == SEL_QUITCTX) {
                uint iter = 1;
                r = cfg.curctx;
                while (iter < CTX_MAX) {
                    (r == CTX_MAX - 1) ? (r = 0) : ++r;
                    if (g_ctx[r].c_cfg.ctxactive) {
                        g_ctx[cfg.curctx].c_cfg.ctxactive = 0;

                        /* Switch to next active context */
                        path = g_ctx[r].c_path;
                        ipath = g_ctx[r].c_init;
                        lastdir = g_ctx[r].c_last;
                        lastname = g_ctx[r].c_name;
                        cfg = g_ctx[r].c_cfg;

                        cfg.curctx = r;
                        setdirwatch();
                        goto begin;
                    }

                    ++iter;
                }
            }

            dentfree(dents);
            return;
        } /* switch (sel) */

        /* Locker */
        if (idletimeout != 0 && idle == idletimeout) {
            idle = 0;
            spawn(utils[LOCKER], NULL, NULL, NULL, F_NORMAL | F_SIGINT);
        }
    }
}

static void usage(void)
{
    fprintf(stdout,
        "usage: nnn [-b key] [-C] [-e] [-i] [-l]\n"
        "           [-p file] [-S] [-v] [-h] [PATH]\n\n"
        "The missing terminal file manager for X.\n\n"
        "positional args:\n"
        "  PATH   start dir [default: current dir]\n\n"
        "optional args:\n"
        " -b key  open bookmark key\n"
        " -C      disable directory color\n"
        " -e      use exiftool for media info\n"
        " -i      nav-as-you-type mode\n"
        " -l      light mode\n"
        " -p file selection file (stdout if '-')\n"
        " -S      disk usage mode\n"
        " -v      show version\n"
        " -h      show help\n\n"
        "v%s\n%s\n", VERSION, GENERAL_INFO);
}

int main(int argc, char *argv[])
{
    char cwd[PATH_MAX] __attribute__ ((aligned));
    char *ipath = NULL;
    int opt;

    while ((opt = getopt(argc, argv, "Slib:Cep:vh")) != -1) {
        switch (opt) {
        case 'S':
            cfg.blkorder = 1;
            nftw_fn = sum_bsizes;
            BLK_SHIFT = ffs(S_BLKSIZE) - 1;
            break;
        case 'l':
            cfg.showdetail = 0;
            printptr = &printent;
            break;
        case 'i':
            cfg.filtermode = 1;
            break;
        case 'b':
            ipath = optarg;
            break;
        case 'C':
            cfg.showcolor = 0;
            break;
        case 'e':
            cfg.metaviewer = EXIFTOOL;
            break;
        case 'p':
            cfg.picker = 1;
            if (optarg[0] == '-' && optarg[1] == '\0')
                cfg.pickraw = 1;
            else {
                /* copier used as tmp var */
                copier = realpath(optarg, g_cppath);
                if (!g_cppath[0]) {
                    fprintf(stderr, "%s\n", strerror(errno));
                    return 1;
                }
            }
            break;
        case 'v':
            fprintf(stdout, "%s\n", VERSION);
            return 0;
        case 'h':
            usage();
            return 0;
        default:
            usage();
            return 1;
        }
    }

    /* Confirm we are in a terminal */
    if (!cfg.picker && !(isatty(0) && isatty(1))) {
        fprintf(stderr, "stdin/stdout !tty\n");
        return 1;
    }

    /* Get the context colors; copier used as tmp var */
    if (cfg.showcolor) {
        copier = xgetenv(env_cfg[NNN_CONTEXT_COLORS], "4444");
        opt = 0;
        while (*copier && opt < CTX_MAX) {
            if (*copier < '0' || *copier > '7') {
                fprintf(stderr, "invalid color code\n");
                return 1;
            }

            g_ctx[opt].color = *copier - '0';
            ++copier;
            ++opt;
        }

        while (opt != CTX_MAX) {
            g_ctx[opt].color = 4;
            ++opt;
        }
    }

    /* Parse bookmarks string */
     if (!parsebmstr()) {
        fprintf(stderr, "%s: 1 char per key\n", env_cfg[NNN_BMS]);
        return 1;
     }

    if (ipath) { /* Open a bookmark directly */
        if (ipath[1] || get_bm_loc(*ipath, cwd) == NULL) {
            fprintf(stderr, "%s\n", messages[STR_INVBM_KEY]);
            return 1;
        }

        ipath = cwd;
    } else if (argc == optind) {
        /* Start in the current directory */
        ipath = getcwd(cwd, PATH_MAX);
        if (ipath == NULL)
            ipath = "/";
    } else {
        ipath = realpath(argv[optind], cwd);
        if (!ipath) {
            fprintf(stderr, "%s: no such dir\n", argv[optind]);
            return 1;
        }
    }

    /* Increase current open file descriptor limit */
    open_max = max_openfds();

    if (getuid() == 0 || getenv(env_cfg[NNN_SHOW_HIDDEN]))
        cfg.showhidden = 1;

    /* Edit text in EDITOR, if opted */
    if (getenv(env_cfg[NNN_USE_EDITOR]))
        cfg.useeditor = 1;

    /* Get VISUAL/EDITOR */
    editor = xgetenv(envs[VISUAL], xgetenv(envs[EDITOR], "vi"));

    /* Get PAGER */
    pager = xgetenv(envs[PAGER], "less");
    getprogarg(pager, &pager_arg);

    /* Get SHELL */
    shell = xgetenv(envs[SHELL], "sh");
    getprogarg(shell, &shell_arg);

    /* Setup script execution */
    scriptpath = getenv(env_cfg[NNN_SCRIPT]);

#ifdef LINUX_INOTIFY
    /* Initialize inotify */
    inotify_fd = inotify_init1(IN_NONBLOCK);
    if (inotify_fd < 0) {
        fprintf(stderr, "inotify init! %s\n", strerror(errno));
        return 1;
    }
#elif defined(BSD_KQUEUE)
    kq = kqueue();
    if (kq < 0) {
        fprintf(stderr, "kqueue init! %s\n", strerror(errno));
        return 1;
    }
#endif

    /* Get custom opener, if set */
    opener = xgetenv(env_cfg[NNN_OPENER], utils[OPENER]);

    /* Get locker wait time, if set; copier used as tmp var */
    copier = getenv(env_cfg[NNN_IDLE_TIMEOUT]);
    if (copier) {
        opt = atoi(copier);
        idletimeout = opt * ((opt > 0) - (opt < 0));
    }

    /* Get the clipboard copier, if set */
    copier = getenv(env_cfg[NNN_COPIER]);

    if (getenv("HOME"))
        g_tmpfplen = xstrlcpy(g_tmpfpath, getenv("HOME"), HOME_LEN_MAX);
    else if (getenv("TMPDIR"))
        g_tmpfplen = xstrlcpy(g_tmpfpath, getenv("TMPDIR"), HOME_LEN_MAX);
    else if (xdiraccess("/tmp"))
        g_tmpfplen = xstrlcpy(g_tmpfpath, "/tmp", HOME_LEN_MAX);

    if (!cfg.picker && g_tmpfplen) {
        xstrlcpy(g_cppath, g_tmpfpath, HOME_LEN_MAX);
        xstrlcpy(g_cppath + g_tmpfplen - 1, "/.nnncp", HOME_LEN_MAX - g_tmpfplen);
    }

    /* Disable auto-select if opted */
    if (getenv(env_cfg[NNN_NO_AUTOSELECT]))
        cfg.autoselect = 0;

    /* Disable opening files on right arrow and `l` */
    if (getenv(env_cfg[NNN_RESTRICT_NAV_OPEN]))
        cfg.nonavopen = 1;

    /* Restrict opening of 0-byte files */
    if (getenv(env_cfg[NNN_RESTRICT_0B]))
        cfg.restrict0b = 1;

    /* Use string-comparison in filter mode */
    if (getenv(env_cfg[NNN_PLAIN_FILTER])) {
        cfg.filter_re = 0;
        filterfn = &visible_str;
    }

    signal(SIGINT, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);

    /* Test initial path */
    if (!xdiraccess(ipath)) {
        fprintf(stderr, "%s: %s\n", ipath, strerror(errno));
        return 1;
    }

    /* Set locale */
    setlocale(LC_ALL, "");
    crc8init();

#ifdef DEBUGMODE
    enabledbg();
#endif
    if (!initcurses())
        return 1;

    browse(ipath);
    exitcurses();

    if (cfg.pickraw) {
        if (copybufpos) {
            opt = selectiontofd(1);
            if (opt != (int)(copybufpos))
                fprintf(stderr, "%s\n", strerror(errno));
        }
    } else if (!cfg.picker && g_cppath[0])
        unlink(g_cppath);

    /* Free the copy buffer */
    free(pcopybuf);

#ifdef LINUX_INOTIFY
    /* Shutdown inotify */
    if (inotify_wd >= 0)
        inotify_rm_watch(inotify_fd, inotify_wd);
    close(inotify_fd);
#elif defined(BSD_KQUEUE)
    if (event_fd >= 0)
        close(event_fd);
    close(kq);
#endif

#ifdef DEBUGMODE
    disabledbg();
#endif
    return 0;
}

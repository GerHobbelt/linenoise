#ifndef SYNTAX_H
#define SYNTAX_H
/**
 * syntax highlight
 *
 * (c) 2017-2020 Steve Bennett <steveb@workware.net.au>
 *
 * See utf8.c for license details.
 */
#ifdef __cplusplus
extern "C" {
#endif

#define HL_NONPRINT 0   /* Non printable character */
#define HL_NORMAL 37    /* White */
#define HL_COMMENT 32   /* Single line comment. Green*/
#define HL_MLCOMMENT 32 /* Multi-line comment.  Green*/
#define HL_KEYWORD1 34  /* Blue */
#define HL_KEYWORD2 36  /* prefix  Cyan*/
#define HL_STRING 35    /* magenta */
#define HL_NUMBER 31    /* red */
#define HL_MATCH 33     /* Search match. Yellow */

#define HL_HIGHLIGHT_STRINGS (1 << 0)
#define HL_HIGHLIGHT_NUMBERS (1 << 1)

/* Tcl/Tk syntax definition */
static char *C_HL_extensions[] = {".tcl", ".tm", NULL};
static char *C_HL_keywords[] = {
    /* Tcl Keywords */
    "after", "append", "apply", "array", "auto_execok", "auto_import", "auto_load",
    "auto_mkindex", "auto_mkindex_old", "auto_qualify", "auto_reset", "bgerror",
    "binary", "break", "catch", "cd", "chan", "clock", "close", "concat", "continue",
    "dde", "dict", "encoding", "eof", "error", "eval", "exec", "exit", "expr",
    "fblocked", "fcopy", "file", "fileevent", "filename", "flush", "for", "foreach",
    "format", "gets", "glob", "global", "history", "http", "if", "incr", "info",
    "interp", "join", "lappend", "lassign", "lindex", "linsert", "list", "llength",
    "load", "loadTk", "lrange", "lrepeat", "lreplace", "lreverse", "lsearch", "lset",
    "lsort", "mathfunc", "mathop", "memory", "msgcat", "namespace", "open", "package",
    "parray", "pid", "pkg::create", "pkg_mkIndex", "platform", "platform::shell",
    "proc", "puts", "pwd", "re_syntax", "read", "refchan", "regexp", "regsub",
    "rename", "return", "registry", "scan", "seek", "set", "socket", "source", "split",
    "string", "subst", "switch", "tcl_endOfWord", "tcl_findLibrary", "tcl_startOfNextWord",
    "tcl_startOfPreviousWord", "tcl_wordBreakAfter", "tcl_wordBreakBefore", "tcltest",
    "tell", "time", "tm", "trace", "unknown", "unload", "unset", "update", "uplevel",
    "upvar", "variable", "vwait", "while",

    /* Tk Keywords */
    "bell", "bind", "bindtags", "bitmap", "button", "canvas", "checkbutton",
    "clipboard", "colors", "console", "cursors", "destroy", "entry", "event",
    "focus", "font", "frame", "grab", "grid", "image", "keysyms", "labelframe",
    "label", "listbox", "loadTk", "lower", "message", "menu", "menubutton",
    "option", "options", "pack", "panedwindow", "photo", "place", "proc",
    "raise", "radiobutton", "scrollbar", "scale", "selection", "send",
    "spinbox", "text", "toplevel", "ttk::button", "ttk::checkbutton",
    "ttk::combobox", "ttk::entry", "ttk::frame", "ttk::intro", "ttk::label",
    "ttk::labelframe", "ttk::notebook", "ttk::panedwindow", "ttk::progressbar",
    "ttk::radiobutton", "ttk::scale", "ttk::scrollbar", "ttk::separator",
    "ttk::sizegrip", "ttk::spinbox", "ttk::style", "ttk::treeview",
    "ttk::widget", "ttk_image", "ttk_vsapi", "tk", "tk::mac", "tk_bisque",
    "tk_chooseColor", "tk_chooseDirectory", "tk_dialog", "tk_error",
    "tk_focusFollowsMouse", "tk_focusNext", "tk_focusPrev", "tk_getOpenFile",
    "tk_getSaveFile", "tk_menuSetFocus", "tk_messageBox", "tk_optionMenu",
    "tk_popup", "tk_setPalette", "tk_textCopy", "tk_textCut", "tk_textPaste",
    "tk_vars", "tk_wait", "tcl_findLibrary", "tcl_startOfNextWord",
    "tcl_startOfPreviousWord", "tcl_wordBreakAfter", "tcl_wordBreakBefore",
    "tcl_endOfWord", "ttk::intro", "ttk_image", "ttk_vsapi", "unload", "uplevel",
    "upvar", "variable", "vwait", "winfo", "wm",

    /* prefix */
    "$|", "{*}|", "$|", NULL};

struct editorSyntax
{
    char **filematch;
    char **keywords;
    char singleline_comment_1[1];
    char singleline_comment_2[2];
    char multiline_comment_start[3];
    char multiline_comment_end[3];
    int flags;
};

/* This structure represents a single line of the file we are editing. */
typedef struct erow
{
    int size;          /* Size of the row, excluding the null term. */
    int rsize;         /* Size of the rendered row. */
    char *chars;       /* Row content. */
    char *render;      /* Row content "rendered" for screen (for TABs). */
    unsigned char *hl; /* Syntax highlight type for each character in render.*/
} erow;



/* We define a very simple "append buffer" structure, that is an heap
 * allocated string where we can append to. This is useful in order to
 * write all the escape sequences in a buffer and flush them to the standard
 * output in a single call, to avoid flickering effects. */
struct abuf
{
    char *b;
    int len;
};

#define ABUF_INIT \
    {             \
        NULL, 0   \
    }

void evalRow(char *s);

void editorRefreshScreen(struct abuf *ab);

void abFree(struct abuf *ab);


#ifdef __cplusplus
}
#endif

#endif

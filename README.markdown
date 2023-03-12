# Linenoise for RISC OS

The original [Linenoise](https://github.com/antirez/linenoise) library is a minimal `readline` replacement, under BSD license. It was focused on providing a VT based editor for Unix-like systems.

This repository ports the library to RISC OS, using its VDU codes, and adds functionality to allow it to be used as a replacement for the `OS_ReadLine` interface used by the majority of command line applications.

Although the repository is a departure from the original library, it should still be possible (albeit a little more involved) to be able to update the library from the upstream.

## RISC OS module

The RISC OS module, called `LineNoise`, provides an implementation of the `ReadLineV` interface used by the `OS_ReadLine` calls used by most RISC OS command line programs. It allows cursor editing of the input line, and supports history.

It has failings:

* May not function correctly when invoked in multiple TaskWindows.

The module has been produced to provide a C-based implementation of a ReadLine system, rather than the assembler-only version provided in the ReadLine module (previously in the Kernel), and the [LineEditor module](https://github.com/philpem/LineEditor).


## Releases

The repository will automatically produce releases for any tags beginning with a `v`. This means that the releases that can be found on the [GitHub pages](https://github.com/gerph/linenoise/releases) will be up to date with the sources.

In addition to the source archives which are always present on releases, the binary release archive contains:

* A built version of the libraries in 26bit, 32bit, application and module variants.
* The header for the library.
* A built version of the example program
* A built version of the LineNoise module.


## Core library

Creating the RISC OS module has required a number of feature changes to the library interface:

* The line editor's history and configuration can now be instantiated separately (the `linenoiseConfig*` function provide this more flexible interface).
* Maximum line length is now configurable.
* Masked mode can now select the character that is to be displayed, rather than just forcing the use of `*`.
* Editing operations which fail can call a registered callback function, or will beep by default. Callbacks are provided for insertions, deletions, completion requests, history movement and cursor movement.
* Insertion into the buffer can now check for the acceptability of the character being inserted, through a callback function.
* History can be cleared, and can be enumerated.
* Error codes used by the library are now more strongly defined.
* Restricts the output to the window width - 1 to avoid scrolling unnecessarily.

There remain some failings:

* Multi-line editing currently isn't reliable.
* Hints are not tested.
* Callbacks do not contexts to indicate which context is in use.

## The API

Linenoise is very easy to use, and reading the example shipped with the
library should get you up to speed ASAP. Here is a list of API calls
and how to use them.

    char *linenoise(const char *prompt);

This is the main Linenoise call: it shows the user a prompt with line editing
and history capabilities. The prompt you specify is used as a prompt, that is,
it will be printed to the left of the cursor. The library returns a buffer
with the line composed by the user, or NULL on end of file or when there
is an out of memory condition.

The returned line should be freed with the `linenoiseFree()` function to make
sure the line is freed with the same allocator it was created.

The canonical loop used by a program using Linenoise will be something like
this:

    while((line = linenoise("hello> ")) != NULL) {
        printf("You wrote: %s\n", line);
        linenoiseFree(line); /* Or just free(line) if you use libc malloc. */
    }

## Single line VS multi line editing

By default, Linenoise uses single line editing, that is, a single row on the
screen will be used, and as the user types more, the text will scroll towards
left to make room. This works if your program is one where the user is
unlikely to write a lot of text, otherwise multi line editing, where multiple
screens rows are used, can be a lot more comfortable.

In order to enable multi line editing use the following API call:

    linenoiseSetMultiLine(1);

You can disable it using `0` as argument.

*Note:* This is not a reliable operation under the RISC OS port at present.


## History

Linenoise supporst history, so that the user does not have to retype
again and again the same things, but can use the down and up arrows in order
to search and re-edit already inserted lines of text.

The followings are the history API calls:

    int linenoiseHistoryAdd(const char *line);
    int linenoiseHistorySetMaxLen(int len);
    int linenoiseHistorySave(const char *filename);
    int linenoiseHistoryLoad(const char *filename);
    void linenoiseHistoryClear(void);
    const char *linenoiseHistoryGetLine(int index);

Use `linenoiseHistoryAdd` every time you want to add a new element
to the top of the history (it will be the first the user will see when
using the up arrow).

Note that for history to work, you have to set a length for the history
(which is zero by default, so history will be disabled if you don't set
a proper one). This is accomplished using the `linenoiseHistorySetMaxLen`
function.

Linenoise has direct support for persisting the history into an history
file. The functions `linenoiseHistorySave` and `linenoiseHistoryLoad` do
just that. Both functions return -1 on error and 0 on success. The load
function will append to the current history. If you wish to start with
a clean history, call the `linenoiseHistoryClear` function first.

To read the history lines (as you might if displaying the history to
the user), the `linenoiseHistoryGetLine` function can be called. It
takes an index from the most recent line which should be accessed, and
returns NULL is no history line is available. If the index is negative.
the lines will be returned from the oldest to the newest.

## Mask mode

Sometimes it is useful to allow the user to type passwords or other
secrets that should not be displayed. For such situations linenoise supports
a "mask mode" that will just replace the characters the user is typing
with `*` characters, like in the following example:

    $ ./linenoise_example
    hello> get mykey
    echo: 'get mykey'
    hello> /mask
    hello> *********

You can enable and disable mask mode using the following two functions:

    void linenoiseMaskModeEnable(void);
    void linenoiseMaskModeDisable(void);

For more control, the character used to mask the output can be supplied
if the default is not suitable:

    void linenoiseMaskModeChar(void);


## Completion

Linenoise supports completion, which is the ability to complete the user
input when she or he presses the `<TAB>` key.

In order to use completion, you need to register a completion callback, which
is called every time the user presses `<TAB>`. Your callback will return a
list of items that are completions for the current string.

The following is an example of registering a completion callback:

    linenoiseSetCompletionCallback(completion);

The completion must be a function returning `void` and getting as input
a `const char` pointer, which is the line the user has typed so far, and
a `linenoiseCompletions` object pointer, which is used as argument of
`linenoiseAddCompletion` in order to add completions inside the callback.
An example will make it more clear:

    void completion(const char *buf, linenoiseCompletions *lc) {
        if (buf[0] == 'h') {
            linenoiseAddCompletion(lc,"hello");
            linenoiseAddCompletion(lc,"hello there");
        }
    }

Basically in your completion callback, you inspect the input, and return
a list of items that are good completions by using `linenoiseAddCompletion`.

If you want to test the completion feature, compile the example program
with `make`, run it, type `h` and press `<TAB>`.

## Hints

Linenoise has a feature called *hints* which is very useful when you
use Linenoise in order to implement a REPL (Read Eval Print Loop) for
a program that accepts commands and arguments, but may also be useful in
other conditions.

The feature shows, on the right of the cursor, as the user types, hints that
may be useful. The hints can be displayed using a different color compared
to the color the user is typing, and can also be bold.

For example as the user starts to type `"git remote add"`, with hints it's
possible to show on the right of the prompt a string `<name> <url>`.

The feature works similarly to the history feature, using a callback.
To register the callback we use:

    linenoiseSetHintsCallback(hints);

The callback itself is implemented like this:

    char *hints(const char *buf, int *color, int *bold) {
        if (!strcasecmp(buf,"git remote add")) {
            *color = 35;
            *bold = 0;
            return " <name> <url>";
        }
        return NULL;
    }

The callback function returns the string that should be displayed or NULL
if no hint is available for the text the user currently typed. The returned
string will be trimmed as needed depending on the number of columns available
on the screen.

It is possible to return a string allocated in dynamic way, by also registering
a function to deallocate the hint string once used:

    void linenoiseSetFreeHintsCallback(linenoiseFreeHintsCallback *);

The free hint callback will just receive the pointer and free the string
as needed (depending on how the hits callback allocated it).

As you can see in the example above, a `color` (in xterm color terminal codes)
can be provided together with a `bold` attribute. If no color is set, the
current terminal foreground color is used. If no bold attribute is set,
non-bold text is printed.

Color codes are:

    red = 31
    green = 32
    yellow = 33
    blue = 34
    magenta = 35
    cyan = 36
    white = 37

*Note:* Color and bold information is not used by the RISC OS port. Hints
have not been tested heavily.

## Insertion acceptability

For some inputs, it is useful to be able to vet the input for acceptability,
and not allow certain inputs to be used. This is might be used to restrict
the input to just digits for a number, for example. A callback function is
used to check whether the character being inserted is suitable for the
buffer. The callback can be registered with:

    void linenoiseSetInsertCallback(linenoiseInsertCallback *);

The callback is called when a new character is inserted into the buffer,
and supplies the character, the current buffer, and the index into the buffer
that the character will be inserted. The callback should return 1 if the
chacater is acceptable, or 0 to reject it. Vetting the input to only allow
numbers might be implemented as:

    int insert_numbers_only(char c, const char *buffer, int pos)
    {
        return (c >= '0' && c <= '9');
    }


## Rejected user operations

Some user operations will not be actioned by the library. Examples might be:

* Attempting a completion when no candidates are available.
* Inserting a character when the buffer is already full.
* Deleting a character when the cursor is at the end of the buffer.
* Moving beyond the start or end of the history.
* Moving beyond the start of end of the buffer.

These will by default issue a bell to the terminal to indicate that the
operation was not performed. However, this may not be suitable for all uses
and can be configured through the use of a callback function. The callback
can be registered with:

    void linenoiseSetFailCallback(linenoiseFailCallback *);

The function is called with a parameter indicating the type of failure.
Alternative forms of notification can therefore be provided to the user
if necessary.

## Multiple configurations

The original Linenoise library allows only a single configuration and
history. This interface has been retained, but all the interfaces are
able duplicated to allow them to be used with a custom configuration
and history. All the `linenoise` prefixed functions have
`linenoiseConfig` prefixed variants which take a configuration pointer
as their first parameter.

New configuration contexts can be created and freed with:

    struct linenoiseConfig *linenoiseNewConfig(void);
    void linenoiseFreeConfig(struct linenoiseConfig *config);

It is the user's responsibility to free configurations after use.

## Screen handling

Sometimes you may want to clear the screen as a result of something the
user typed. You can do this by calling the following function:

    void linenoiseClearScreen(void);

## Related projects

* [Linenoise](https://github.com/antirez/linenoise) is the original version of Linenoise.
* [Linenoise NG](https://github.com/arangodb/linenoise-ng) is a fork of Linenoise that aims to add more advanced features like UTF-8 support, Windows support and other features. Uses C++ instead of C as development language.
* [Linenoise-swift](https://github.com/andybest/linenoise-swift) is a reimplementation of Linenoise written in Swift.

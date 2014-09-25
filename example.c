#define _POSIX_C_SOURCE 200112L
#define _XOPEN_SOURCE 500
#define _BSD_SOURCE

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#ifdef __cplusplus_cli
#include <vcclr.h>
#endif
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <ctype.h>
#include <locale.h>
#include "linenoise.h"

#ifdef _WIN32
#include <io.h>
#include <conio.h>

#define bool int
#define true 1
#define false 0
#else
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/select.h>
#define TCHAR char
#define _T(x) x
#define _tcsncmp strncmp
#define _tstoi atoi
#define _tprintf printf
#endif

#define POLL_TIMEOUT_MS 10000

static bool do_exit = false;

#ifdef _WIN32
HANDLE wakeup_event;
#endif

static void completion(const TCHAR *buf, size_t pos, linenoiseCompletions *lc) {
    (void) pos;
    if (_tcsncmp(buf, _T("multi kulti"), 11) == 0) {
        /* No hints */
    } else if (_tcsncmp(buf, _T("multi"), 5) == 0 && isspace(buf[5])) {
        linenoiseAddCompletion(lc, _T("kulti"), _T("multi kulti"), SIZE_MAX);
    } else if (_tcsncmp(buf, _T("hello"), 5) == 0 && isspace(buf[5])) {
        if (buf[5] == _T('\0') || (isspace(buf[5]) && (buf[6] == _T('\0') || buf[6] == _T('t'))))
            linenoiseAddCompletion(lc, _T("there"), _T("hello there"), SIZE_MAX);
        if (buf[5] == _T('\0') || (isspace(buf[5]) && (buf[6] == _T('\0') || buf[6] == _T('h'))))
            linenoiseAddCompletion(lc, _T("here"), _T("hello here"), SIZE_MAX);
    } else {
        if (buf[0] == _T('h') || buf[0] == _T('\0'))
            linenoiseAddCompletion(lc, _T("hello"), _T("hello "), SIZE_MAX);
        if (buf[0] == _T('m') || buf[0] == _T('\0'))
            linenoiseAddCompletion(lc, _T("multi"), _T("multi "), SIZE_MAX);
    }
}

static void sigint_handler(int signum)
{
    (void) signum;
    linenoiseCancel();
}

#ifndef _WIN32
static void sigwinch_handler(int signum)
{
    (void) signum;
    linenoiseUpdateSize();
}

static void sigalrm_handler(int signum)
{
    (void) signum;
    /* This signal just wakes-up the poll method, so that the method linenoise */
    /* gets called - this differentiates handling of the ESC key from the ANSI */
    /* escape sequences. */
}
#endif

#ifdef _WIN32
#ifdef __cplusplus_cli
delegate BOOL ConsoleHandler(DWORD CtrlType);
static BOOL console_handler(DWORD win_event)
#else
static BOOL WINAPI console_handler(DWORD win_event)
#endif
{
	switch (win_event)
	{
	case CTRL_C_EVENT: sigint_handler(SIGINT); break;
	case CTRL_BREAK_EVENT: sigint_handler(SIGINT); break;
	default: break;
	}
	SetEvent(wakeup_event);
    return TRUE;
}
#endif

void sigterm_handler(int signum)
{
    (void) signum;
    do_exit = true;
}

int main(int argc, char **argv) {
    TCHAR *line;
    char *prgname = argv[0];
    bool async = false;
#ifdef _WIN32
    HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    DWORD found_error = 0;
#else
    struct sigaction sa;
    int found_error = 0;
#endif

#ifdef __cplusplus_cli
    gcroot<ConsoleHandler^> *console_handler_ptr;
    System::IntPtr console_handler_intptr;

    console_handler_ptr = new gcroot<ConsoleHandler^>(gcnew ConsoleHandler(console_handler));
    console_handler_intptr =
        System::Runtime::InteropServices::Marshal::GetFunctionPointerForDelegate(
            static_cast<ConsoleHandler^>(*console_handler_ptr));
#endif

    setlocale(LC_CTYPE, "");

    /* Parse options, with --multiline we enable multi line editing. */
    while(argc > 1) {
        argc--;
        argv++;
        if (!strcmp(*argv,"--multiline")) {
            linenoiseSetMultiLine(1);
            printf("Multi-line mode enabled.\n");
        } else  if (!strcmp(*argv,"--async")) {
#ifdef _WIN32
            linenoiseSetAsync(true);
#else
            int flagsRead = fcntl(STDIN_FILENO, F_GETFL, 0);
            fcntl(STDIN_FILENO, F_SETFL, flagsRead | O_NONBLOCK);
#endif
            async = true;
            printf("Asynchronous mode enabled.\n");
        } else {
            fprintf(stderr, "Usage: %s [--multiline] [--async]\n", prgname);
            exit(1);
        }
    }

    setlocale(LC_CTYPE, "");

#ifdef _WIN32
    wakeup_event = CreateEventA(NULL, FALSE, FALSE, NULL);
#ifdef __cplusplus_cli
    SetConsoleCtrlHandler(static_cast<PHANDLER_ROUTINE>(console_handler_intptr.ToPointer()), TRUE);
#else
    SetConsoleCtrlHandler(console_handler, TRUE);
#endif
#else
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigint_handler;
    sigaction(SIGINT, &sa, NULL);
    sa.sa_handler = sigwinch_handler;
    sigaction(SIGWINCH, &sa, NULL);
    sa.sa_handler = sigalrm_handler;
    sigaction(SIGALRM, &sa, NULL);
    sa.sa_handler = sigterm_handler;
    sigaction(SIGTERM, &sa, NULL);
#endif

    /* Set the prompt. */
    linenoiseSetPrompt(_T("hello> "));

    /* Set the completion callback. This will be called every time the
     * user uses the <tab> key. */
    linenoiseSetCompletionCallback(completion);

    /* Load history from file. The history file is just a plain text file
     * where entries are separated by newlines. */
    linenoiseHistoryLoad(_T("history.txt")); /* Load the history at startup */

    /* Now this is the main loop of the typical linenoise-based application.
     * The call to linenoise() will block as long as the user types something
     * and presses enter.
     *
     * The typed string is returned as a malloc() allocated string by
     * linenoise, so the user needs to free() it. */

    do {
        if (async) {
            /* Block signals to have a reliable call to linenoiseHasPendingChar */
#ifndef _WIN32
            sigset_t set, oldset;
            sigemptyset(&set);
            sigaddset(&set, SIGINT);
            sigaddset(&set, SIGALRM);
            sigaddset(&set, SIGWINCH);
            pthread_sigmask(SIG_BLOCK, &set, &oldset);
#endif
            linenoiseShowPrompt();

            if (!linenoiseHasPendingChar()) {
#ifdef _WIN32
				HANDLE handles[2] = { input, wakeup_event };
				DWORD result = WaitForMultipleObjects(2, handles, FALSE, POLL_TIMEOUT_MS);
				if ( result == WAIT_TIMEOUT ) {
					linenoiseCustomOutput();
					printf("* Ping\n");
				}
#else
                fd_set fds;
                int selectresult;
                struct timespec tv = { POLL_TIMEOUT_MS / 1000,
                        (POLL_TIMEOUT_MS % 1000) * 1000000L };
                FD_ZERO(&fds);
                FD_SET(STDIN_FILENO, &fds);
                selectresult = pselect(STDIN_FILENO + 1, &fds, NULL, NULL, &tv, &oldset);
                if (selectresult == 0) {
                    linenoiseCustomOutput();
                    printf("* Ping\n");
                }
#endif
			}

#ifndef _WIN32
            pthread_sigmask(SIG_SETMASK, &oldset, NULL);
#endif
		}

#ifdef _WIN32
        SetLastError(ERROR_SUCCESS);
#else
        errno = 0;
#endif
        line = linenoise();
#ifdef _WIN32
        found_error = GetLastError();
#else
        found_error = errno;
#endif
        if (line != NULL) {
            /* Do something with the string. */
            if (line[0] != '\0' && line[0] != '/') {
                if (async)  /* Can be called also in blocking mode (does nothing) */
                    linenoiseCustomOutput();
                _tprintf(_T("echo: '%s'\n"), line);
                linenoiseHistoryAdd(line); /* Add to the history. */
                linenoiseHistorySave(_T("history.txt")); /* Save the history on disk. */
                if (!_tcsncmp(line,_T("exit"),4) && line[4] == _T('\0') ||
                    !_tcsncmp(line,_T("quit"),4) && line[4] == _T('\0')) {
                    do_exit = true;
                }
            } else if (!_tcsncmp(line,_T("/historylen"),11)) {
                /* The "/historylen" command will change the history len. */
                int len = _tstoi(line+11);
                linenoiseHistorySetMaxLen(len);
            } else if (line[0] == '/') {
                if (async)  /* Can be called also in blocking mode (does nothing) */
                    linenoiseCustomOutput();
                _tprintf(_T("Unreconized command: %s\n"), line);
            }
            free(line);
        }
#ifdef _WIN32
    } while ((line != NULL || found_error == ERROR_CONTINUE) && !do_exit);
#else
    } while ((line != NULL || found_error == EWOULDBLOCK
            || found_error == EAGAIN) && !do_exit);
#endif

    linenoiseCleanup();

    if (found_error != 0
#ifdef _WIN32
            && found_error != ERROR_CANCELLED
#else
            && found_error != EINTR
#endif
        ) {
        TCHAR buf[1024];
        buf[0] = _T('\0');
#ifdef _WIN32
        FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, NULL, found_error,
            GetUserDefaultLangID(), buf, sizeof(buf)/sizeof(TCHAR), NULL);
#else
        strerror_r(found_error, buf, sizeof(buf));
#endif
        buf[1023] = _T('\0');
        _tprintf(_T("Error: %s\n"), buf);
    }

#ifdef _WIN32
#ifdef __cplusplus_cli
    SetConsoleCtrlHandler(static_cast<PHANDLER_ROUTINE>(console_handler_intptr.ToPointer()), FALSE);
    delete console_handler_ptr;
#else
    SetConsoleCtrlHandler(console_handler, FALSE);
#endif
    CloseHandle(wakeup_event);
#endif

	return 0;
}

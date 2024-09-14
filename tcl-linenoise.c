#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(_WIN32)

#include <tcl.h>
#include <tk.h>
#include "linenoise.h"

/* The mutex serializing the threads requesting user interaction. */
TCL_DECLARE_MUTEX(edit)

#ifndef NO_COMPLETION
void completion(const char *buf, linenoiseCompletions *lc, void *userdata)
{
    (void)userdata;

    if (buf[0] == 'i')
    {
        linenoiseAddCompletion(lc, "info");
        if (buf[5] == 'h')
            linenoiseAddCompletion(lc, "info hostname");
        if (buf[5] == 's')
            linenoiseAddCompletion(lc, "info sharedlibextension");
        linenoiseAddCompletion(lc, "info tclversion");
        linenoiseAddCompletion(lc, "info patchlevel");
    }
    if (buf[0] == 'p')
    {
        linenoiseAddCompletion(lc, "package ");
        linenoiseAddCompletion(lc, "package names");
        linenoiseAddCompletion(lc, "package require");
    }
    if (buf[0] == 's')
    {
        linenoiseAddCompletion(lc, "set");
    }
}

char *hints(const char *buf, int *color, int *bold, void *userdata)
{
    (void)userdata;
    if (!strcasecmp(buf, "hello"))
    {
        *color = 35;
        *bold = 0;
        return " World";
    }
    return NULL;
}
#endif

static int
Start_Cmd(ClientData cdata, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
    char *retstr = " \
    \n\r************************************  \
    \n\r*  Welcome to \033[1;91mJ \033[92mI \033[93mM \033[96mM \033[95mI\033[0m            * \
    \n\r************************************ \n\r";

    Tcl_SetObjResult(interp, Tcl_NewStringObj(retstr, -1));
    return TCL_OK;
}

static int
ClearScreen_Cmd(ClientData cdata, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
    linenoiseClearScreen();
    return TCL_OK;
}

static int
Prompt_Cmd(ClientData cdata, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
    const char *prompt = "[Tcl] ";
    char *line = "";
    // int code = 0; /*return code from TCL API function like TCL_OK*/

    linenoiseSetMultiLine(1); /*word wrap*/
#ifndef NO_COMPLETION
    /* Set the completion callback. This will be called every time the
     * user uses the <tab> key. */
    linenoiseSetCompletionCallback(completion, NULL);
    linenoiseSetHintsCallback(hints, NULL);
#endif

    Tcl_MutexLock(&edit);
    line = linenoise(prompt);
    Tcl_MutexUnlock(&edit);

    //Tcl_ResetResult(interp);
    if (line == NULL)
    {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("linenoise::prompt aborted", -1));
        Tcl_Eval(interp, "exit");
        return TCL_ERROR;
    }

    if (Tcl_InterpDeleted(interp))
    {
        return TCL_ERROR;
    }

    //Tcl_Eval(interp, line);
    Tcl_EvalEx(interp, line, -1, TCL_EVAL_GLOBAL || TCL_EVAL_DIRECT);
    // Tcl_SetResult(interp, line, TCL_STATIC);
    linenoiseHistoryAdd(line);
    return TCL_OK;
}

int DLLEXPORT
Linenoise_Init(Tcl_Interp *interp)
{

    if (Tcl_InitStubs(interp, TCL_VERSION, 0) == NULL)
    {
        return TCL_ERROR;
    }

    if (Tcl_PkgProvide(interp, "linenoise", "1.0") == TCL_ERROR)
    {
        return TCL_ERROR;
    }

    Tcl_CreateObjCommand(interp, "start", Start_Cmd, NULL, NULL);
    Tcl_CreateObjCommand(interp, "prompt", Prompt_Cmd, NULL, NULL);
    Tcl_CreateObjCommand(interp, "clearscreen", ClearScreen_Cmd, NULL, NULL);
    return TCL_OK;
}

#endif

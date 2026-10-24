#include "i_interface.h"
#include "st_start.h"
#include "gamestate.h"
#include "startupinfo.h"
#include "c_cvars.h"
#include "gstrings.h"

static_assert(sizeof(void*) == 8, "32 builds are not supported");

// Some global engine variables taken out of the backend code.
FStartupScreen* StartWindow;
SystemCallbacks sysCallbacks;
FString endoomName;
bool batchrun;
float menuBlurAmount;

bool AppActive = true;
int chatmodeon;
gamestate_t 	gamestate = GS_STARTUP;
bool ToggleFullscreen;
int 			paused;
bool			pauseext;

FStartupInfo GameStartupInfo;

CVAR(Bool, queryiwad, true, CVAR_ARCHIVE | CVAR_GLOBALCONFIG);
CVAR(String, defaultiwad, "", CVAR_ARCHIVE | CVAR_GLOBALCONFIG);
#if defined(__APPLE__) && HAVE_RT
// the macOS RT build shows the frame rate by default, and remembers when it's turned off
CVAR(Bool, vid_fps, true, CVAR_ARCHIVE)
#else
CVAR(Bool, vid_fps, false, 0)
#endif

EXTERN_CVAR(Bool, ui_generic)

#if !HAVE_RT
CUSTOM_CVAR(String, language, "auto", CVAR_ARCHIVE | CVAR_NOINITCALL | CVAR_GLOBALCONFIG)
#else
CUSTOM_CVAR(String, language, "default", CVAR_ARCHIVE | CVAR_NOINITCALL | CVAR_GLOBALCONFIG)
#endif
{
	GStrings.UpdateLanguage(self);
	UpdateGenericUI(ui_generic);
	if (sysCallbacks.LanguageChanged) sysCallbacks.LanguageChanged(self);
}


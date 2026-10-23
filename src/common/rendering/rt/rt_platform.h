#pragma once

// Platform services of the RT renderer that aren't GZDoom's: creating the RTGL1 instance
// on the game window, message boxes, opening links, window captures.
// Windows: rt_main.cpp (Win32RTVideo). macOS: common/platform/posix/cocoa/i_video.mm.

#include "tarray.h"
#include "v_video.h"

// creates the RTGL1 instance ('rt') for the game window;
// pMetalLayer: macOS, the CAMetalLayer of the window's content view (nullptr on Windows)
void RT_CreateRendererInstance( void* pMetalLayer );
void RT_DestroyRendererInstance();

class SystemBaseFrameBuffer;
// the RT renderer's frame buffer (rtx::RTFrameBuffer), after RT_CreateRendererInstance
SystemBaseFrameBuffer* RT_CreateFrameBuffer( void* hMonitor, bool fullscreen );

#ifndef _WIN32

// a modal message box with an OK button
void RT_PlatformMessageBox( const char* text, const char* caption );
// the last error of the dynamic library loader
const char* RT_PlatformLibraryError();
// the game window is minimized (in the Dock)
bool RT_PlatformIsWindowMinimized();
// the contents of the game window, as RGB rows from the top (empty on failure)
TArray< uint8_t > RT_PlatformCaptureWindow( int& pitch, ESSType& color_type, float& gamma );

#else

inline void RT_PlatformMessageBox( const char* text, const char* caption )
{
    MessageBoxA( nullptr, text, caption, MB_ICONEXCLAMATION | MB_OK );
}

#endif

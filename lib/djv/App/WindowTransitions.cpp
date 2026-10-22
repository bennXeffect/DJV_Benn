// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the DJV project.

#include <djv/App/WindowTransitions.h>

#if defined(_WIN32) && defined(FTK_SDL3)
#include <SDL3/SDL.h>
#ifndef NOMINMAX
#define NOMINMAX
#endif // NOMINMAX
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif // WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dwmapi.h>
#endif // _WIN32

namespace djv
{
    namespace app
    {
        void disableWindowTransitions()
        {
#if defined(_WIN32) && defined(FTK_SDL3)
            int count = 0;
            if (SDL_Window** windows = SDL_GetWindows(&count))
            {
                for (int i = 0; i < count; ++i)
                {
                    const SDL_PropertiesID props = SDL_GetWindowProperties(windows[i]);
                    if (HWND hwnd = static_cast<HWND>(SDL_GetPointerProperty(
                        props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr)))
                    {
                        const BOOL disable = TRUE;
                        DwmSetWindowAttribute(
                            hwnd,
                            DWMWA_TRANSITIONS_FORCEDISABLED,
                            &disable,
                            sizeof(disable));
                    }
                }
                SDL_free(windows);
            }
#endif // _WIN32
        }
    }
}

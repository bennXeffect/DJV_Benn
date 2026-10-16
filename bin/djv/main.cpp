// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the DJV project.

#include <djv/App/App.h>

#include <tlRender/UI/Init.h>

#include <ftk/Core/Context.h>
#include <ftk/Core/Error.h>

#include <iostream>

#if defined(_WINDOWS)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif // WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tchar.h>
#endif // _WINDOWS

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif // WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <timeapi.h>
#endif // _WIN32

FTK_MAIN()
{
#if defined(_WIN32)
    // Waits to the millisecond rather than the system's default of a
    // sixty-fourth of a second. The application's loop and the player's
    // thread each wait a few milliseconds between turns, and a wait of
    // four came back after sixteen: the loop then went whole frames of
    // playback without a turn, and those frames were never shown -- 27 or
    // 28 of 30 a second. For as long as the application runs.
    struct TimerResolution
    {
        TimerResolution() { timeBeginPeriod(1); }
        ~TimerResolution() { timeEndPeriod(1); }
    } timerResolution;
#endif // _WIN32
    try
    {
        auto context = ftk::Context::create();
        tl::ui::init(context);
        auto args = ftk::convert(argc, argv);
        auto app = djv::app::App::create(context, args);
        if (app->hasCmdLineHelp())
            return 0;
        app->run();
    }
    catch(const std::exception& e)
    {
        std::cerr << "ERROR: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}

#if defined(_WINDOWS)
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int)
{
    return wmain(__argc, __wargv);
}
#endif // _WINDOWS

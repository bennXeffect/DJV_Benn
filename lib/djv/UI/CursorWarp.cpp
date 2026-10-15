// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the DJV project.

#include <djv/UI/CursorWarp.h>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif // NOMINMAX
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif // WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif // _WIN32

namespace djv
{
    namespace ui
    {
        bool warpCursor(const ftk::V2I& from, const ftk::V2I& to)
        {
            bool out = false;
#if defined(_WIN32)
            POINT p;
            if (GetCursorPos(&p))
            {
                out = SetCursorPos(p.x + (to.x - from.x), p.y + (to.y - from.y)) != 0;
            }
#endif // _WIN32
            return out;
        }
    }
}

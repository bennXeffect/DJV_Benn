// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the DJV project.

#pragma once

#include <djv/UI/Export.h>

#include <ftk/Core/Vector.h>

namespace djv
{
    namespace ui
    {
        //! Move the system cursor by the difference between two positions
        //! in a window: from where the last event put it to where it should
        //! be. Moved relative to where it is rather than placed, so nothing
        //! needs to know where the window is on the screen. Returns whether
        //! it could be; only on Windows for now.
        DJV_UI_API bool warpCursor(const ftk::V2I& from, const ftk::V2I& to);
    }
}

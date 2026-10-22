// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the DJV project.

#pragma once

namespace djv
{
    namespace app
    {
        //! Turn off the window manager's animations of the application's
        //! windows: leaving full screen, Windows grew the window back from
        //! the corner of the screen, which looked like something going
        //! wrong. Nothing off Windows.
        void disableWindowTransitions();
    }
}

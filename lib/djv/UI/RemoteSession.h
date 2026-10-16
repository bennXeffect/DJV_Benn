// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the DJV project.

#pragma once

namespace djv
{
    namespace ui
    {
        //! Whether a Parsec client is connected to this computer, from the
        //! last connection event in Parsec's log. A remote pointer is
        //! driven by where the client's pointer is while it is visible, so
        //! moving it from here is undone: what needs the pointer moved has
        //! to hide it instead. Always false off Windows.
        bool isParsecConnected();
    }
}

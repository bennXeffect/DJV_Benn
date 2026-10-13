// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the DJV project.

#pragma once

#include <cstdint>
#include <string>

namespace djv
{
    namespace app
    {
        //! Memory in use, for the status bar.
        struct SystemMemory
        {
            //! The memory this process uses.
            uint64_t processRAM = 0;

            //! The memory in use on the whole system, and its total.
            uint64_t systemRAMUsed = 0;
            uint64_t systemRAMTotal = 0;

            //! The video memory this process uses, and the total of the
            //! graphics card. Not every platform can say.
            bool     vramValid = false;
            uint64_t vramUsed = 0;
            uint64_t vramTotal = 0;
        };

        //! Get the memory in use.
        SystemMemory getSystemMemory();

        //! Get the memory in use as text, e.g.
        //! "RAM: 3.2 GB (41.0/63.9 GB)  VRAM: 2.1/12.0 GB".
        std::string getSystemMemoryLabel(const SystemMemory&);
    }
}

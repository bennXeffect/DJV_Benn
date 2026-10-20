// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the DJV project.

#pragma once

#include <djv/UI/Export.h>

#include <tlRender/UI/ThumbnailSystem.h>

namespace djv
{
    namespace models
    {
        class DisplayProcessor;
    }

    namespace ui
    {
        //! The thumbnails' color: each frame sampled down to the thumbnail's
        //! size in floating point, shown as the viewport shows it through
        //! the display processor, then to eight bits. A linear render's
        //! thumbnails had been its file values cut at one, dark and flat.
        DJV_UI_API tl::ui::ThumbnailSystem::ColorFunction getThumbnailColor(
            const std::shared_ptr<models::DisplayProcessor>&);
    }
}

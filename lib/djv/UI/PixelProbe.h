// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the DJV project.

#pragma once

#include <djv/UI/Export.h>

#include <ftk/Core/Color.h>
#include <ftk/Core/Image.h>
#include <ftk/Core/Vector.h>

#include <array>
#include <memory>
#include <optional>
#include <string>

namespace djv
{
    namespace ui
    {
        //! Read a pixel of an image as it is stored -- the file's own
        //! values, unclamped, before any color management. Pixels are
        //! counted from the top left of the picture. Nothing for the YUV
        //! and packed types, which hold no such values.
        DJV_UI_API std::optional<ftk::Color4F> readPixel(
            const std::shared_ptr<ftk::Image>&,
            const ftk::V2I&);

        //! Get the luminance weights for a color space, from its name: the
        //! Rec.2020 and ACES AP1 / AP0 primaries have their own, anything
        //! else is taken as Rec.709 / sRGB.
        DJV_UI_API std::array<float, 3> getLumaWeights(const std::string& colorSpace);

        //! Get the text of the pixel probe, in the manner of Blender's image
        //! sample: the position, the file's values, the displayed values,
        //! their hue, saturation and value, and the luminance.
        DJV_UI_API std::string getPixelProbeText(
            const ftk::V2I& pos,
            const std::optional<ftk::Color4F>& file,
            const std::optional<ftk::Color4F>& display,
            const std::array<float, 3>& lumaWeights);
    }
}

// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the DJV project.

#include <djv/UI/ThumbnailColor.h>

#include <djv/UI/PixelProbe.h>

#include <djv/Models/ColorModel.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace djv
{
    namespace ui
    {
        tl::ui::ThumbnailSystem::ColorFunction getThumbnailColor(
            const std::shared_ptr<models::DisplayProcessor>& processor)
        {
            if (!processor)
            {
                return nullptr;
            }
            return [processor](
                const ftk::Path& path,
                const std::shared_ptr<ftk::Image>& image,
                const ftk::Size2I& size) -> std::shared_ptr<ftk::Image>
            {
                if (!image || !size.isValid())
                {
                    return nullptr;
                }
                const ftk::ImageInfo& info = image->getInfo();
                const int w = info.size.w;
                const int h = info.size.h;
                if (w <= 0 || h <= 0)
                {
                    return nullptr;
                }

                // Each thumbnail pixel the mean of a grid of samples across
                // the pixels it covers: a box filter, in floating point, so
                // what is past one is still there for the view to bring
                // down. A few samples a side rather than every pixel, which
                // on a 4K frame is several hundred for each.
                const int samples = 3;
                std::vector<float> rgba(static_cast<size_t>(size.w) * size.h * 4);
                for (int y = 0; y < size.h; ++y)
                {
                    for (int x = 0; x < size.w; ++x)
                    {
                        float sum[4] = { 0.F, 0.F, 0.F, 0.F };
                        int n = 0;
                        for (int sy = 0; sy < samples; ++sy)
                        {
                            const int py = std::min(h - 1, static_cast<int>(
                                (y + (sy + .5F) / samples) / size.h * h));
                            for (int sx = 0; sx < samples; ++sx)
                            {
                                const int px = std::min(w - 1, static_cast<int>(
                                    (x + (sx + .5F) / samples) / size.w * w));
                                const auto c = readPixel(image, ftk::V2I(px, py));
                                if (!c.has_value())
                                {
                                    // A type with no such values: scaled as
                                    // it is.
                                    return nullptr;
                                }
                                sum[0] += std::isfinite(c->r) ? c->r : 0.F;
                                sum[1] += std::isfinite(c->g) ? c->g : 0.F;
                                sum[2] += std::isfinite(c->b) ? c->b : 0.F;
                                sum[3] += std::isfinite(c->a) ? c->a : 1.F;
                                ++n;
                            }
                        }
                        float* out = &rgba[(static_cast<size_t>(y) * size.w + x) * 4];
                        for (int k = 0; k < 4; ++k)
                        {
                            out[k] = sum[k] / n;
                        }
                    }
                }

                processor->apply(
                    path.get(),
                    image->getTags(),
                    rgba.data(),
                    static_cast<size_t>(size.w) * size.h);

                // To eight bits, the rows bottom first as the renderer has
                // them: the samples above were taken from the top.
                auto out = ftk::Image::create(size.w, size.h, ftk::ImageType::RGBA_U8);
                uint8_t* data = out->getData();
                for (int y = 0; y < size.h; ++y)
                {
                    const float* src = &rgba[static_cast<size_t>(y) * size.w * 4];
                    uint8_t* dst = data + static_cast<size_t>(size.h - 1 - y) * size.w * 4;
                    for (int x = 0; x < size.w * 4; ++x)
                    {
                        dst[x] = static_cast<uint8_t>(std::clamp(src[x], 0.F, 1.F) * 255.F + .5F);
                    }
                }
                return out;
            };
        }
    }
}

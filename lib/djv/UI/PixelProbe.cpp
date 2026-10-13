// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the DJV project.

#include <djv/UI/PixelProbe.h>

#include <ftk/Core/Format.h>
#include <ftk/Core/Memory.h>
#include <ftk/Core/String.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace djv
{
    namespace ui
    {
        namespace
        {
            float halfToFloat(uint16_t h)
            {
                const uint32_t sign = (h & 0x8000U) << 16;
                const uint32_t exponent = (h >> 10) & 0x1FU;
                const uint32_t mantissa = h & 0x3FFU;
                if (0 == exponent)
                {
                    // Zero and the subnormals, which are the mantissa in
                    // units of two to the minus twenty four.
                    const float value = std::ldexp(static_cast<float>(mantissa), -24);
                    return sign ? -value : value;
                }
                uint32_t bits = 0;
                if (31 == exponent)
                {
                    bits = sign | 0x7F800000U | (mantissa << 13);
                }
                else
                {
                    bits = sign | ((exponent + 112) << 23) | (mantissa << 13);
                }
                float out = 0.F;
                std::memcpy(&out, &bits, sizeof(out));
                return out;
            }

            template<typename T>
            T load(const uint8_t* p)
            {
                T out;
                std::memcpy(&out, p, sizeof(T));
                return out;
            }

            float channel(const uint8_t* p, int bytes, bool isFloat)
            {
                switch (bytes)
                {
                case 1: return p[0] / 255.F;
                case 2: return isFloat ?
                    halfToFloat(load<uint16_t>(p)) :
                    load<uint16_t>(p) / 65535.F;
                case 4: return isFloat ?
                    load<float>(p) :
                    static_cast<float>(load<uint32_t>(p) / 4294967295.0);
                default: break;
                }
                return 0.F;
            }

            ftk::Color4F toHSV(const ftk::Color4F& c)
            {
                const float r = std::clamp(c.r, 0.F, 1.F);
                const float g = std::clamp(c.g, 0.F, 1.F);
                const float b = std::clamp(c.b, 0.F, 1.F);
                const float max = std::max({ r, g, b });
                const float min = std::min({ r, g, b });
                const float d = max - min;
                float h = 0.F;
                if (d > 0.F)
                {
                    if (max == r)
                    {
                        h = std::fmod((g - b) / d, 6.F);
                    }
                    else if (max == g)
                    {
                        h = (b - r) / d + 2.F;
                    }
                    else
                    {
                        h = (r - g) / d + 4.F;
                    }
                    h /= 6.F;
                    if (h < 0.F)
                    {
                        h += 1.F;
                    }
                }
                const float s = max > 0.F ? d / max : 0.F;
                return ftk::Color4F(h, s, max, c.a);
            }
        }

        std::optional<ftk::Color4F> readPixel(
            const std::shared_ptr<ftk::Image>& image,
            const ftk::V2I& pos)
        {
            if (!image || !image->getData())
            {
                return std::nullopt;
            }
            const ftk::ImageInfo& info = image->getInfo();
            const int w = info.size.w;
            const int h = info.size.h;
            if (pos.x < 0 || pos.y < 0 || pos.x >= w || pos.y >= h)
            {
                return std::nullopt;
            }
            // The renderer's own order is OpenGL's, the bottom row first;
            // mirror.y marks data stored the other way up, as OpenEXR and
            // the movie readers store it. The position is from the top.
            const int x = info.layout.mirror.x ? (w - 1 - pos.x) : pos.x;
            const int y = info.layout.mirror.y ? pos.y : (h - 1 - pos.y);

            int channels = 0;
            int bytes = 0;
            bool isFloat = false;
            bool planar = false;
            switch (info.type)
            {
            case ftk::ImageType::L_U8:     channels = 1; bytes = 1; break;
            case ftk::ImageType::L_U16:    channels = 1; bytes = 2; break;
            case ftk::ImageType::L_U32:    channels = 1; bytes = 4; break;
            case ftk::ImageType::L_F16:    channels = 1; bytes = 2; isFloat = true; break;
            case ftk::ImageType::L_F32:    channels = 1; bytes = 4; isFloat = true; break;
            case ftk::ImageType::LA_U8:    channels = 2; bytes = 1; break;
            case ftk::ImageType::LA_U16:   channels = 2; bytes = 2; break;
            case ftk::ImageType::LA_U32:   channels = 2; bytes = 4; break;
            case ftk::ImageType::LA_F16:   channels = 2; bytes = 2; isFloat = true; break;
            case ftk::ImageType::LA_F32:   channels = 2; bytes = 4; isFloat = true; break;
            case ftk::ImageType::RGB_U8:   channels = 3; bytes = 1; break;
            case ftk::ImageType::RGB_U16:  channels = 3; bytes = 2; break;
            case ftk::ImageType::RGB_U32:  channels = 3; bytes = 4; break;
            case ftk::ImageType::RGB_F16:  channels = 3; bytes = 2; isFloat = true; break;
            case ftk::ImageType::RGB_F32:  channels = 3; bytes = 4; isFloat = true; break;
            case ftk::ImageType::RGBA_U8:  channels = 4; bytes = 1; break;
            case ftk::ImageType::RGBA_U16: channels = 4; bytes = 2; break;
            case ftk::ImageType::RGBA_U32: channels = 4; bytes = 4; break;
            case ftk::ImageType::RGBA_F16: channels = 4; bytes = 2; isFloat = true; break;
            case ftk::ImageType::RGBA_F32: channels = 4; bytes = 4; isFloat = true; break;
            case ftk::ImageType::RGB_F16_P:
                channels = 3; bytes = 2; isFloat = true; planar = true; break;
            default:
                return std::nullopt;
            }

            const size_t alignment = std::max(info.layout.alignment, 1);
            const uint8_t* data = image->getData();
            float v[4] = { 0.F, 0.F, 0.F, 1.F };
            if (planar)
            {
                const size_t rowBytes = ftk::getAlignedByteCount(
                    static_cast<size_t>(w) * bytes, alignment);
                const size_t planeBytes = rowBytes * h;
                for (int c = 0; c < channels; ++c)
                {
                    const uint8_t* p = data + planeBytes * c + rowBytes * y + static_cast<size_t>(x) * bytes;
                    v[c] = channel(p, bytes, isFloat);
                }
            }
            else
            {
                const size_t pixelBytes = static_cast<size_t>(channels) * bytes;
                const size_t rowBytes = ftk::getAlignedByteCount(
                    static_cast<size_t>(w) * pixelBytes, alignment);
                const uint8_t* p = data + rowBytes * y + static_cast<size_t>(x) * pixelBytes;
                for (int c = 0; c < channels; ++c)
                {
                    v[c] = channel(p + c * bytes, bytes, isFloat);
                }
            }

            ftk::Color4F out;
            switch (channels)
            {
            case 1: out = ftk::Color4F(v[0], v[0], v[0], 1.F); break;
            case 2: out = ftk::Color4F(v[0], v[0], v[0], v[1]); break;
            case 3: out = ftk::Color4F(v[0], v[1], v[2], 1.F); break;
            default: out = ftk::Color4F(v[0], v[1], v[2], v[3]); break;
            }
            return out;
        }

        std::array<float, 3> getLumaWeights(const std::string& colorSpace)
        {
            const std::string s = ftk::toLower(colorSpace);
            if (s.find("2020") != std::string::npos ||
                s.find("2100") != std::string::npos)
            {
                return { .2627F, .678F, .0593F };
            }
            if (s.find("acescg") != std::string::npos ||
                s.find("ap1") != std::string::npos ||
                s.find("acescct") != std::string::npos ||
                s.find("acescc") != std::string::npos)
            {
                return { .2722287F, .6740818F, .0536895F };
            }
            if (s.find("aces2065") != std::string::npos ||
                s.find("ap0") != std::string::npos)
            {
                return { .3439664F, .7281661F, -.0721325F };
            }
            return { .2126F, .7152F, .0722F };
        }

        std::string getPixelProbeText(
            const ftk::V2I& pos,
            const std::optional<ftk::Color4F>& file,
            const std::optional<ftk::Color4F>& display,
            const std::array<float, 3>& lumaWeights)
        {
            std::string out = ftk::Format("X:{0} Y:{1}").
                arg(pos.x, 5).
                arg(pos.y, 5);
            if (file.has_value())
            {
                const ftk::Color4F& c = file.value();
                out += ftk::Format("  |  R:{0} G:{1} B:{2} A:{3}").
                    arg(c.r, 4, 7).
                    arg(c.g, 4, 7).
                    arg(c.b, 4, 7).
                    arg(c.a, 4, 7).str();
            }
            if (display.has_value())
            {
                const ftk::Color4F& c = display.value();
                const ftk::Color4F hsv = toHSV(c);
                // A second line: the whole of it on one was wider than the
                // view of a laptop.
                out += ftk::Format("\nCM R:{0} G:{1} B:{2}").
                    arg(c.r, 3, 5).
                    arg(c.g, 3, 5).
                    arg(c.b, 3, 5).str();
                out += ftk::Format("  |  H:{0} S:{1} V:{2}").
                    arg(hsv.r, 3, 5).
                    arg(hsv.g, 3, 5).
                    arg(hsv.b, 3, 5).str();
            }
            if (file.has_value())
            {
                const ftk::Color4F& c = file.value();
                const float l =
                    c.r * lumaWeights[0] +
                    c.g * lumaWeights[1] +
                    c.b * lumaWeights[2];
                out += ftk::Format("  |  L:{0}").arg(l, 4, 7).str();
            }
            return out;
        }
    }
}

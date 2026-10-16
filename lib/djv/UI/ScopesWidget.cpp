// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the DJV project.

#include <djv/UI/ScopesWidget.h>

#include <djv/UI/PixelProbe.h>

#include <djv/Models/ColorModel.h>

#include <tlRender/Timeline/ColorOptions.h>
#include <tlRender/Timeline/Player.h>

#include <ftk/Core/FontSystem.h>
#include <ftk/Core/Format.h>
#include <ftk/Core/RenderOptions.h>

#if defined(TLRENDER_OCIO)
#include <OpenColorIO/OpenColorIO.h>
#endif // TLRENDER_OCIO

#include <algorithm>
#include <chrono>
#include <cmath>
#include <future>

#if defined(TLRENDER_OCIO)
namespace OCIO = OCIO_NAMESPACE;
#endif // TLRENDER_OCIO

namespace djv
{
    namespace ui
    {
        FTK_ENUM_IMPL(
            ScopesSource,
            "Display",
            "File");

        namespace
        {
            // The resolution of the scopes themselves, scaled to the widget
            // when drawn.
            const int histogramW = 256;
            const int histogramH = 100;
            const int waveformW = 256;
            const int waveformH = 128;
            const int vectorscopeSize = 160;

            // About this many pixels are measured, however large the frame:
            // enough for the shape of every scope, few enough to keep up
            // with playback.
            const int sampleW = 512;
            const int sampleH = 288;

            // The heights the scopes are drawn at, before the display scale.
            const int histogramDrawH = 90;
            const int waveformDrawH = 110;
            const int vectorscopeDrawH = 150;

            std::shared_ptr<ftk::Image> createImage(int w, int h)
            {
                auto out = ftk::Image::create(w, h, ftk::ImageType::RGBA_U8);
                out->zero();
                return out;
            }

            uint8_t toU8(float value)
            {
                return static_cast<uint8_t>(std::clamp(value, 0.F, 1.F) * 255.F + .5F);
            }

            // Rows are stored bottom first, the renderer's order.
            void setPixel(
                const std::shared_ptr<ftk::Image>& image,
                int x,
                int y,
                float r,
                float g,
                float b,
                float a)
            {
                const ftk::ImageInfo& info = image->getInfo();
                uint8_t* p = image->getData() + (static_cast<size_t>(y) * info.size.w + x) * 4;
                p[0] = toU8(r);
                p[1] = toU8(g);
                p[2] = toU8(b);
                p[3] = toU8(a);
            }

            //! What the measurement works from: its own references, so the
            //! widget can go on while it runs.
            struct ScopesJob
            {
                std::shared_ptr<ftk::Image> frame;
#if defined(TLRENDER_OCIO)
                OCIO::ConstCPUProcessorRcPtr processor;
#endif // TLRENDER_OCIO
                ScopesSource source = ScopesSource::Display;
            };

            struct ScopesResult
            {
                std::shared_ptr<ftk::Image> histogram;
                std::shared_ptr<ftk::Image> waveform;
                std::shared_ptr<ftk::Image> vectorscope;
            };

            //! Measures the frame: run off the main thread, where it held
            //! playback up -- with the display transform applied on the CPU,
            //! tens of milliseconds a time, ten times a second.
            ScopesResult measure(const ScopesJob& job)
            {
                ScopesResult out;
                if (!job.frame)
                {
                    return out;
                }
                const ftk::ImageInfo& info = job.frame->getInfo();
                const int w = info.size.w;
                const int h = info.size.h;
                if (w <= 0 || h <= 0)
                {
                    return out;
                }

                // Sample the frame on a grid.
                const int stepX = std::max(1, w / sampleW);
                const int stepY = std::max(1, h / sampleH);
                const int cols = (w + stepX - 1) / stepX;
                const int rows = (h + stepY - 1) / stepY;
                std::vector<float> values;
                values.reserve(static_cast<size_t>(cols) * rows * 4);
                std::vector<int> xs;
                xs.reserve(static_cast<size_t>(cols) * rows);
                for (int y = 0; y < h; y += stepY)
                {
                    for (int x = 0; x < w; x += stepX)
                    {
                        const auto c = readPixel(job.frame, ftk::V2I(x, y));
                        if (!c.has_value())
                        {
                            return out;
                        }
                        values.push_back(c->r);
                        values.push_back(c->g);
                        values.push_back(c->b);
                        values.push_back(c->a);
                        xs.push_back(x);
                    }
                }
                const size_t count = xs.size();
                if (0 == count)
                {
                    return out;
                }
#if defined(TLRENDER_OCIO)
                if (ScopesSource::Display == job.source && job.processor)
                {
                    OCIO::PackedImageDesc desc(
                        values.data(),
                        static_cast<long>(count),
                        1,
                        4);
                    job.processor->apply(desc);
                }
#endif // TLRENDER_OCIO

                // Histogram: the red, green and blue counts over the range from
                // zero to one, values past it in the last bin.
                std::vector<int> hist(256 * 3, 0);
                // Waveform: the value of each channel by column.
                std::vector<int> wave(static_cast<size_t>(waveformW) * waveformH * 3, 0);
                // Vectorscope: the Rec.709 chroma of each sample.
                std::vector<int> vec(static_cast<size_t>(vectorscopeSize) * vectorscopeSize, 0);
                for (size_t i = 0; i < count; ++i)
                {
                    const float* v = &values[i * 4];
                    const int wx = std::min(waveformW - 1, xs[i] * waveformW / w);
                    for (int k = 0; k < 3; ++k)
                    {
                        const float c = std::clamp(v[k], 0.F, 1.F);
                        ++hist[k * 256 + std::min(255, static_cast<int>(c * 255.F + .5F))];
                        const int wy = std::min(waveformH - 1, static_cast<int>(c * (waveformH - 1) + .5F));
                        ++wave[(static_cast<size_t>(wy) * waveformW + wx) * 3 + k];
                    }
                    const float r = std::clamp(v[0], 0.F, 1.F);
                    const float g = std::clamp(v[1], 0.F, 1.F);
                    const float b = std::clamp(v[2], 0.F, 1.F);
                    const float cb = -.1146F * r - .3854F * g + .5F * b;
                    const float cr = .5F * r - .4542F * g - .0458F * b;
                    const int vx = std::clamp(static_cast<int>((cb + .5F) * (vectorscopeSize - 1) + .5F), 0, vectorscopeSize - 1);
                    const int vy = std::clamp(static_cast<int>((cr + .5F) * (vectorscopeSize - 1) + .5F), 0, vectorscopeSize - 1);
                    ++vec[static_cast<size_t>(vy) * vectorscopeSize + vx];
                }

                // Histogram image. Scaled to the tallest bin away from the two
                // ends, which a frame with a black or a clipped background fills
                // and which would otherwise flatten everything else.
                {
                    int max = 1;
                    for (int k = 0; k < 3; ++k)
                    {
                        for (int i = 1; i < 255; ++i)
                        {
                            max = std::max(max, hist[k * 256 + i]);
                        }
                    }
                    out.histogram = createImage(histogramW, histogramH);
                    for (int x = 0; x < histogramW; ++x)
                    {
                        float heights[3];
                        for (int k = 0; k < 3; ++k)
                        {
                            heights[k] = std::min(1.F, hist[k * 256 + x] / static_cast<float>(max)) * histogramH;
                        }
                        for (int y = 0; y < histogramH; ++y)
                        {
                            const float r = y < heights[0] ? .8F : 0.F;
                            const float g = y < heights[1] ? .8F : 0.F;
                            const float b = y < heights[2] ? .8F : 0.F;
                            if (r > 0.F || g > 0.F || b > 0.F)
                            {
                                setPixel(out.histogram, x, y, r, g, b, .9F);
                            }
                        }
                    }
                }

                // Waveform image, the channels added together, brighter where
                // more of the column has that value.
                {
                    const float perColumn = count / static_cast<float>(waveformW);
                    const float scale = std::log1p(std::max(1.F, perColumn * .1F));
                    out.waveform = createImage(waveformW, waveformH);
                    for (int y = 0; y < waveformH; ++y)
                    {
                        for (int x = 0; x < waveformW; ++x)
                        {
                            const int* c = &wave[(static_cast<size_t>(y) * waveformW + x) * 3];
                            if (c[0] || c[1] || c[2])
                            {
                                const float r = std::log1p(static_cast<float>(c[0])) / scale;
                                const float g = std::log1p(static_cast<float>(c[1])) / scale;
                                const float b = std::log1p(static_cast<float>(c[2])) / scale;
                                setPixel(out.waveform, x, y, r, g, b, std::max({ r, g, b }));
                            }
                        }
                    }
                }

                // Vectorscope image.
                {
                    int max = 1;
                    for (size_t i = 0; i < vec.size(); ++i)
                    {
                        // The center is every grey pixel, which would swamp the
                        // rest.
                        const int x = static_cast<int>(i % vectorscopeSize);
                        const int y = static_cast<int>(i / vectorscopeSize);
                        if (std::abs(x - vectorscopeSize / 2) > 1 || std::abs(y - vectorscopeSize / 2) > 1)
                        {
                            max = std::max(max, vec[i]);
                        }
                    }
                    const float scale = std::log1p(static_cast<float>(max));
                    out.vectorscope = createImage(vectorscopeSize, vectorscopeSize);
                    for (int y = 0; y < vectorscopeSize; ++y)
                    {
                        for (int x = 0; x < vectorscopeSize; ++x)
                        {
                            const int n = vec[static_cast<size_t>(y) * vectorscopeSize + x];
                            if (n > 0)
                            {
                                const float i = std::min(1.F, std::log1p(static_cast<float>(n)) / scale);
                                setPixel(out.vectorscope, x, y, .6F + .4F * i, 1.F, .6F + .4F * i, .3F + .7F * i);
                            }
                        }
                    }
                }
                return out;
            }
        }

        struct ScopesWidget::Private
        {
            std::shared_ptr<models::ColorModel> colorModel;
            ScopesSource source = ScopesSource::Display;

            std::shared_ptr<ftk::Image> frame;
            bool dirty = true;
            bool processorDirty = true;
            std::chrono::steady_clock::time_point lastCompute;
#if defined(TLRENDER_OCIO)
            OCIO::ConstCPUProcessorRcPtr processor;
#endif // TLRENDER_OCIO

            std::shared_ptr<ftk::Image> histogram;
            std::shared_ptr<ftk::Image> waveform;
            std::shared_ptr<ftk::Image> vectorscope;
            //! The measurement running, one at a time; a frame that comes
            //! meanwhile is measured after it.
            std::future<ScopesResult> measuring;

            struct SizeData
            {
                bool init = true;
                float displayScale = 0.F;
                int margin = 0;
                int spacing = 0;
                int border = 0;
                ftk::FontInfo fontInfo;
                ftk::FontMetrics fontMetrics;
                ftk::Size2I sizeHint;
            };
            SizeData size;

            std::shared_ptr<ftk::ListObserver<tl::VideoFrame> > videoObserver;
            std::shared_ptr<ftk::Observer<tl::OCIOOptions> > ocioOptionsObserver;
            std::shared_ptr<ftk::Observer<std::vector<std::string> > > resolvedInputsObserver;
        };

        void ScopesWidget::_init(
            const std::shared_ptr<ftk::Context>& context,
            const std::shared_ptr<models::ColorModel>& colorModel,
            const std::shared_ptr<IWidget>& parent)
        {
            IWidget::_init(context, "djv::ui::ScopesWidget", parent);
            FTK_P();
            p.colorModel = colorModel;

            p.ocioOptionsObserver = ftk::Observer<tl::OCIOOptions>::create(
                colorModel->observeOCIOOptions(),
                [this](const tl::OCIOOptions&)
                {
                    _p->processorDirty = true;
                    _p->dirty = true;
                });
            p.resolvedInputsObserver = ftk::Observer<std::vector<std::string> >::create(
                colorModel->observeResolvedInputs(),
                [this](const std::vector<std::string>&)
                {
                    _p->processorDirty = true;
                    _p->dirty = true;
                });
        }

        ScopesWidget::ScopesWidget() :
            _p(new Private)
        {}

        ScopesWidget::~ScopesWidget()
        {}

        std::shared_ptr<ScopesWidget> ScopesWidget::create(
            const std::shared_ptr<ftk::Context>& context,
            const std::shared_ptr<models::ColorModel>& colorModel,
            const std::shared_ptr<IWidget>& parent)
        {
            auto out = std::shared_ptr<ScopesWidget>(new ScopesWidget);
            out->_init(context, colorModel, parent);
            return out;
        }

        void ScopesWidget::setPlayer(const std::shared_ptr<tl::Player>& player)
        {
            FTK_P();
            p.frame.reset();
            p.dirty = true;
            p.videoObserver.reset();
            if (player)
            {
                p.videoObserver = ftk::ListObserver<tl::VideoFrame>::create(
                    player->observeCurrentVideo(),
                    [this](const std::vector<tl::VideoFrame>& value)
                    {
                        FTK_P();
                        std::shared_ptr<ftk::Image> frame;
                        if (!value.empty())
                        {
                            for (const auto& layer : value.front().layers)
                            {
                                if (layer.image)
                                {
                                    frame = layer.image;
                                    break;
                                }
                            }
                        }
                        if (frame != p.frame)
                        {
                            p.frame = frame;
                            p.dirty = true;
                        }
                    });
            }
        }

        void ScopesWidget::setSource(ScopesSource value)
        {
            FTK_P();
            if (value == p.source)
                return;
            p.source = value;
            p.dirty = true;
        }

        ftk::Size2I ScopesWidget::getSizeHint() const
        {
            return _p->size.sizeHint;
        }

        void ScopesWidget::setGeometry(const ftk::Box2I& value)
        {
            IWidget::setGeometry(value);
        }

        void ScopesWidget::sizeHintEvent(const ftk::SizeHintEvent& event)
        {
            IWidget::sizeHintEvent(event);
            FTK_P();
            if (p.size.init || p.size.displayScale != event.displayScale)
            {
                p.size.init = false;
                p.size.displayScale = event.displayScale;
                p.size.margin = event.style->getSizeRole(ftk::SizeRole::MarginSmall, event.displayScale);
                p.size.spacing = event.style->getSizeRole(ftk::SizeRole::SpacingSmall, event.displayScale);
                p.size.border = event.style->getSizeRole(ftk::SizeRole::Border, event.displayScale);
                p.size.fontInfo = event.style->getFont(ftk::FontType::Regular, event.displayScale);
                p.size.fontMetrics = event.fontSystem->getMetrics(p.size.fontInfo);
            }
            const int w = std::lround(280 * event.displayScale);
            const int line = p.size.fontMetrics.lineHeight;
            p.size.sizeHint.w = w + p.size.margin * 2;
            p.size.sizeHint.h =
                p.size.margin * 2 +
                (line + p.size.spacing) * 3 +
                std::lround(histogramDrawH * event.displayScale) +
                std::lround(waveformDrawH * event.displayScale) +
                std::lround(vectorscopeDrawH * event.displayScale) +
                p.size.spacing * 2;
        }

        void ScopesWidget::tickEvent(
            bool parentsVisible,
            bool parentsEnabled,
            const ftk::TickEvent& event)
        {
            IWidget::tickEvent(parentsVisible, parentsEnabled, event);
            FTK_P();
            // Measured at most ten times a second, and only while shown: the
            // scopes cost nothing when the tool is closed.
            if (p.measuring.valid() &&
                std::future_status::ready == p.measuring.wait_for(std::chrono::seconds(0)))
            {
                ScopesResult result = p.measuring.get();
                p.histogram = result.histogram;
                p.waveform = result.waveform;
                p.vectorscope = result.vectorscope;
                setDrawUpdate();
            }
            const auto now = std::chrono::steady_clock::now();
            if (p.dirty &&
                !p.measuring.valid() &&
                parentsVisible &&
                isVisible(false) &&
                now - p.lastCompute >= std::chrono::milliseconds(100))
            {
                p.lastCompute = now;
                p.dirty = false;
                _processorUpdate();
                ScopesJob job;
                job.frame = p.frame;
#if defined(TLRENDER_OCIO)
                job.processor = p.processor;
#endif // TLRENDER_OCIO
                job.source = p.source;
                p.measuring = std::async(
                    std::launch::async,
                    [job]
                    {
                        return measure(job);
                    });
            }
        }

        void ScopesWidget::drawEvent(
            const ftk::Box2I& drawRect,
            const ftk::DrawEvent& event)
        {
            IWidget::drawEvent(drawRect, event);
            FTK_P();
            const ftk::Box2I g = ftk::margin(getGeometry(), -p.size.margin);
            const int line = p.size.fontMetrics.lineHeight;
            const int histH = std::lround(histogramDrawH * event.displayScale);
            const int waveH = std::lround(waveformDrawH * event.displayScale);
            const int vecH = std::lround(vectorscopeDrawH * event.displayScale);
            const ftk::Color4F textColor = event.style->getColorRole(ftk::ColorRole::Text);
            const ftk::Color4F bgColor(0.F, 0.F, 0.F, 1.F);
            const ftk::Color4F gridColor(1.F, 1.F, 1.F, .15F);
            ftk::ImageOptions imageOptions;
            imageOptions.cache = false;

            const std::string sourceLabel = ScopesSource::Display == p.source ?
                " (display)" : " (file values)";
            int y = g.min.y;
            const auto title = [&](const std::string& text)
            {
                event.render->drawText(
                    event.fontSystem->getGlyphs(text, p.size.fontInfo),
                    p.size.fontMetrics,
                    ftk::V2I(g.min.x, y),
                    textColor);
                y += line + p.size.spacing;
            };

            // Histogram.
            title("Histogram" + sourceLabel);
            ftk::Box2I box(g.min.x, y, g.w(), histH);
            event.render->drawRect(box, bgColor);
            for (int i = 1; i < 4; ++i)
            {
                const int x = box.min.x + box.w() * i / 4;
                event.render->drawRect(ftk::Box2I(x, box.min.y, p.size.border, box.h()), gridColor);
            }
            if (p.histogram)
            {
                event.render->drawImage(p.histogram, box, ftk::Color4F(1.F, 1.F, 1.F), imageOptions);
            }
            y += histH + p.size.spacing;

            // Waveform.
            title("Waveform" + sourceLabel);
            box = ftk::Box2I(g.min.x, y, g.w(), waveH);
            event.render->drawRect(box, bgColor);
            for (int i = 1; i < 4; ++i)
            {
                const int yy = box.min.y + box.h() * i / 4;
                event.render->drawRect(ftk::Box2I(box.min.x, yy, box.w(), p.size.border), gridColor);
            }
            if (p.waveform)
            {
                event.render->drawImage(p.waveform, box, ftk::Color4F(1.F, 1.F, 1.F), imageOptions);
            }
            y += waveH + p.size.spacing;

            // Vectorscope, square and centered.
            title("Vectorscope" + sourceLabel);
            const int s = std::min(g.w(), vecH);
            box = ftk::Box2I(g.min.x + (g.w() - s) / 2, y, s, s);
            event.render->drawRect(box, bgColor);
            const ftk::V2F c(box.min.x + s / 2.F, box.min.y + s / 2.F);
            const float r = s / 2.F - 1.F;
            std::vector<std::pair<ftk::V2F, ftk::V2F> > lines;
            const int segments = 64;
            for (int i = 0; i < segments; ++i)
            {
                const float a0 = i / static_cast<float>(segments) * 6.2831853F;
                const float a1 = (i + 1) / static_cast<float>(segments) * 6.2831853F;
                lines.push_back(std::make_pair(
                    ftk::V2F(c.x + std::cos(a0) * r, c.y + std::sin(a0) * r),
                    ftk::V2F(c.x + std::cos(a1) * r, c.y + std::sin(a1) * r)));
            }
            lines.push_back(std::make_pair(ftk::V2F(c.x - r, c.y), ftk::V2F(c.x + r, c.y)));
            lines.push_back(std::make_pair(ftk::V2F(c.x, c.y - r), ftk::V2F(c.x, c.y + r)));
            event.render->drawLines(lines, gridColor);
            if (p.vectorscope)
            {
                event.render->drawImage(p.vectorscope, box, ftk::Color4F(1.F, 1.F, 1.F), imageOptions);
            }
            // The targets of the primaries and secondaries at full
            // saturation, where a pure color of each lands.
            const struct
            {
                float r, g, b;
            } targets[] =
            {
                { 1.F, 0.F, 0.F }, { 1.F, 1.F, 0.F }, { 0.F, 1.F, 0.F },
                { 0.F, 1.F, 1.F }, { 0.F, 0.F, 1.F }, { 1.F, 0.F, 1.F }
            };
            for (const auto& t : targets)
            {
                const float cb = -.1146F * t.r - .3854F * t.g + .5F * t.b;
                const float cr = .5F * t.r - .4542F * t.g - .0458F * t.b;
                const ftk::V2F pos(c.x + cb * 2.F * r, c.y - cr * 2.F * r);
                const float h = 3.F * event.displayScale;
                event.render->drawRect(
                    ftk::Box2F(pos.x - h, pos.y - h, h * 2.F, h * 2.F),
                    ftk::Color4F(t.r, t.g, t.b, .8F));
            }
        }

        void ScopesWidget::_processorUpdate()
        {
            FTK_P();
#if defined(TLRENDER_OCIO)
            // The display transform the viewport applies, rebuilt when the
            // color options or the file's input color space change.
            if (p.processorDirty)
            {
                p.processorDirty = false;
                p.processor.reset();
                const tl::OCIOOptions options = p.colorModel->getOCIOOptions();
                if (options.enabled)
                {
                    try
                    {
                        OCIO::ConstConfigRcPtr config;
                        switch (options.config)
                        {
                        case tl::OCIOConfig::BuiltIn:
                            config = OCIO::Config::CreateFromFile("ocio://default");
                            break;
                        case tl::OCIOConfig::EnvVar:
                            if (tl::hasOCIOEnvVar())
                            {
                                config = OCIO::Config::CreateFromEnv();
                            }
                            break;
                        case tl::OCIOConfig::File:
                            config = OCIO::Config::CreateFromFile(options.fileName.c_str());
                            break;
                        default: break;
                        }
                        if (config)
                        {
                            std::string input = options.input;
                            const auto& resolved = p.colorModel->observeResolvedInputs()->get();
                            if (input.empty() && !resolved.empty())
                            {
                                input = resolved.front();
                            }
                            if (input.empty())
                            {
                                input = OCIO::ROLE_SCENE_LINEAR;
                            }
                            std::string display = options.display;
                            if (display.empty())
                            {
                                display = config->getDefaultDisplay();
                            }
                            std::string view = options.view;
                            if (view.empty())
                            {
                                view = config->getDefaultView(display.c_str());
                            }
                            auto dvt = OCIO::DisplayViewTransform::Create();
                            dvt->setSrc(input.c_str());
                            dvt->setDisplay(display.c_str());
                            dvt->setView(view.c_str());
                            auto pipeline = OCIO::LegacyViewingPipeline::Create();
                            pipeline->setDisplayViewTransform(dvt);
                            if (!options.look.empty())
                            {
                                pipeline->setLooksOverrideEnabled(true);
                                pipeline->setLooksOverride(options.look.c_str());
                            }
                            auto processor = pipeline->getProcessor(
                                config,
                                config->getCurrentContext());
                            p.processor = processor->getDefaultCPUProcessor();
                        }
                    }
                    catch (const std::exception&)
                    {
                        p.processor.reset();
                    }
                }
            }
#endif // TLRENDER_OCIO
        }
    }
}

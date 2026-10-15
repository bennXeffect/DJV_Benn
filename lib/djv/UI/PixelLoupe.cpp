// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the DJV project.

#include <djv/UI/PixelLoupe.h>

#include <djv/UI/ViewportBinding.h>

#include <tlRender/Timeline/Player.h>
#include <tlRender/UI/Viewport.h>

#include <ftk/Core/FontSystem.h>
#include <ftk/Core/Format.h>

namespace djv
{
    namespace ui
    {
        struct PixelLoupe::Private
        {
            std::shared_ptr<tl::ui::Viewport> viewport;
            std::shared_ptr<ViewportBinding> binding;
            ftk::V2I target;
            int average = 0;

            struct SizeData
            {
                bool init = true;
                float displayScale = 0.F;
                int margin = 0;
                int border = 0;
                ftk::FontInfo fontInfo;
                ftk::FontMetrics fontMetrics;
            };
            SizeData size;

            std::shared_ptr<ftk::ListObserver<tl::VideoFrame> > videoObserver;
        };

        void PixelLoupe::_init(
            const std::shared_ptr<ftk::Context>& context,
            const std::shared_ptr<models::FilesModel>& filesModel,
            const std::shared_ptr<models::ColorModel>& colorModel,
            const std::shared_ptr<models::ViewportModel>& viewportModel,
            const std::shared_ptr<IWidget>& parent)
        {
            IWidget::_init(context, "djv::ui::PixelLoupe", parent);
            FTK_P();

            p.viewport = tl::ui::Viewport::create(context, shared_from_this());
            p.viewport->setInputEnabled(false);

            // Bound like the viewport, so the picture is drawn the same
            // way, except that the pixels are drawn as blocks: a pixel
            // smoothed into its neighbors is not one that can be pointed at.
            p.binding = ViewportBinding::create(
                filesModel,
                colorModel,
                viewportModel,
                p.viewport);
            std::weak_ptr<tl::ui::Viewport> viewportWeak(p.viewport);
            const auto nearest = [viewportWeak]
            {
                if (auto viewport = viewportWeak.lock())
                {
                    auto options = viewport->getImageOptions();
                    bool changed = false;
                    for (auto& i : options)
                    {
                        if (i.imageFilters.magnify != ftk::ImageFilter::Nearest)
                        {
                            i.imageFilters.magnify = ftk::ImageFilter::Nearest;
                            changed = true;
                        }
                    }
                    if (changed)
                    {
                        viewport->setImageOptions(options);
                    }
                }
            };
            p.binding->setChangedCallback(nearest);
            nearest();
        }

        PixelLoupe::PixelLoupe() :
            _p(new Private)
        {}

        PixelLoupe::~PixelLoupe()
        {}

        std::shared_ptr<PixelLoupe> PixelLoupe::create(
            const std::shared_ptr<ftk::Context>& context,
            const std::shared_ptr<models::FilesModel>& filesModel,
            const std::shared_ptr<models::ColorModel>& colorModel,
            const std::shared_ptr<models::ViewportModel>& viewportModel,
            const std::shared_ptr<IWidget>& parent)
        {
            auto out = std::shared_ptr<PixelLoupe>(new PixelLoupe);
            out->_init(context, filesModel, colorModel, viewportModel, parent);
            return out;
        }

        void PixelLoupe::setPlayer(const std::shared_ptr<tl::Player>& value)
        {
            FTK_P();
            p.viewport->setPlayer(value);
            if (value)
            {
                p.videoObserver = ftk::ListObserver<tl::VideoFrame>::create(
                    value->observeCurrentVideo(),
                    [this](const std::vector<tl::VideoFrame>& value)
                    {
                        _p->binding->setVideoFramesSize(value.size());
                    });
            }
            else
            {
                p.videoObserver.reset();
                p.binding->setVideoFramesSize(0);
            }
            _viewUpdate();
        }

        void PixelLoupe::setTarget(const ftk::V2I& value)
        {
            FTK_P();
            if (value == p.target)
                return;
            p.target = value;
            _viewUpdate();
        }

        void PixelLoupe::setAverage(int value)
        {
            FTK_P();
            const int tmp = std::clamp(value, 0, 2);
            if (tmp == p.average)
                return;
            p.average = tmp;
            setDrawUpdate();
        }

        std::optional<ftk::Color4F> PixelLoupe::getDisplayColor()
        {
            FTK_P();
            std::optional<ftk::Color4F> out;
            const ftk::Box2I& g = p.viewport->getGeometry();
            if (!g.isValid())
            {
                return out;
            }
            // The middle of the middle pixel's block, and of each of its
            // neighbors the average takes in.
            const ftk::V2I c(g.w() / 2, g.h() / 2);
            ftk::Color4F sum(0.F, 0.F, 0.F, 0.F);
            int count = 0;
            for (int y = -p.average; y <= p.average; ++y)
            {
                for (int x = -p.average; x <= p.average; ++x)
                {
                    const ftk::Color4F s = p.viewport->getColorSample(
                        ftk::V2I(c.x + x * zoom, c.y + y * zoom));
                    sum.r += s.r;
                    sum.g += s.g;
                    sum.b += s.b;
                    sum.a += s.a;
                    ++count;
                }
            }
            if (count > 0)
            {
                out = ftk::Color4F(sum.r / count, sum.g / count, sum.b / count, sum.a / count);
            }
            return out;
        }

        ftk::Size2I PixelLoupe::getLoupeSize() const
        {
            FTK_P();
            const int square = pixels * zoom + p.size.border * 2;
            return ftk::Size2I(
                square,
                square + p.size.fontMetrics.lineHeight + p.size.margin * 2);
        }

        ftk::Size2I PixelLoupe::getSizeHint() const
        {
            return getLoupeSize();
        }

        void PixelLoupe::setGeometry(const ftk::Box2I& value)
        {
            IWidget::setGeometry(value);
            FTK_P();
            // The note on top, the picture under it.
            const int note = p.size.fontMetrics.lineHeight + p.size.margin * 2;
            const int square = pixels * zoom;
            p.viewport->setGeometry(ftk::Box2I(
                value.min.x + p.size.border,
                value.min.y + note + p.size.border,
                square,
                square));
            _viewUpdate();
        }

        void PixelLoupe::sizeHintEvent(const ftk::SizeHintEvent& event)
        {
            IWidget::sizeHintEvent(event);
            FTK_P();
            if (p.size.init || p.size.displayScale != event.displayScale)
            {
                p.size.init = false;
                p.size.displayScale = event.displayScale;
                p.size.margin = event.style->getSizeRole(ftk::SizeRole::MarginInside, event.displayScale);
                p.size.border = std::max(2, event.style->getSizeRole(ftk::SizeRole::Border, event.displayScale));
                p.size.fontInfo = event.style->getFont(ftk::FontType::Regular, event.displayScale);
                p.size.fontMetrics = event.fontSystem->getMetrics(p.size.fontInfo);
            }
        }

        void PixelLoupe::drawEvent(
            const ftk::Box2I& drawRect,
            const ftk::DrawEvent& event)
        {
            IWidget::drawEvent(drawRect, event);
            FTK_P();
            const ftk::Box2I& g = getGeometry();
            const int note = p.size.fontMetrics.lineHeight + p.size.margin * 2;

            // The note: how many pixels the values are an average of.
            const int n = p.average * 2 + 1;
            const std::string text = 0 == p.average ?
                std::string("1 pixel") :
                std::string(ftk::Format("{0}x{1} pixels averaged").arg(n).arg(n));
            const ftk::Size2I textSize = event.fontSystem->getSize(text, p.size.fontInfo);
            const ftk::Box2I noteBox(
                g.min.x,
                g.min.y,
                std::min(g.w(), textSize.w + p.size.margin * 2),
                note);
            event.render->drawRect(noteBox, event.style->getColorRole(ftk::ColorRole::Overlay));
            event.render->drawText(
                event.fontSystem->getGlyphs(text, p.size.fontInfo),
                p.size.fontMetrics,
                ftk::V2I(g.min.x + p.size.margin, g.min.y + p.size.margin),
                event.style->getColorRole(ftk::ColorRole::Text));

            // The frame around the picture.
            const ftk::Box2I square(
                g.min.x,
                g.min.y + note,
                g.w(),
                g.h() - note);
            event.render->drawRect(square, ftk::Color4F(0.F, 0.F, 0.F, 1.F));
        }

        void PixelLoupe::drawOverlayEvent(
            const ftk::Box2I& drawRect,
            const ftk::DrawEvent& event)
        {
            IWidget::drawOverlayEvent(drawRect, event);
            FTK_P();
            const ftk::Box2I& v = p.viewport->getGeometry();
            if (!v.isValid())
                return;

            // The box around the pixels averaged, and the crosshair up to
            // it. Drawn twice, dark under light, to show on any picture.
            const ftk::V2I c(v.min.x + v.w() / 2, v.min.y + v.h() / 2);
            const int half = (p.average * 2 + 1) * zoom / 2;
            const ftk::Box2I box(c.x - half - 1, c.y - half - 1, half * 2 + 2, half * 2 + 2);
            const auto outline = [&event](const ftk::Box2I& b, int w, const ftk::Color4F& color)
            {
                std::vector<ftk::Box2I> rects;
                rects.push_back(ftk::Box2I(b.min.x, b.min.y, b.w(), w));
                rects.push_back(ftk::Box2I(b.min.x, b.max.y - w + 1, b.w(), w));
                rects.push_back(ftk::Box2I(b.min.x, b.min.y, w, b.h()));
                rects.push_back(ftk::Box2I(b.max.x - w + 1, b.min.y, w, b.h()));
                event.render->drawRects(rects, color);
            };
            const ftk::Color4F dark(0.F, 0.F, 0.F, .8F);
            const ftk::Color4F light(1.F, 1.F, 1.F, 1.F);
            outline(ftk::margin(box, 1), 3, dark);
            outline(box, 1, light);

            const int gap = half + 3;
            std::vector<ftk::Box2I> darkLines =
            {
                ftk::Box2I(c.x - 1, v.min.y, 3, c.y - gap - v.min.y),
                ftk::Box2I(c.x - 1, c.y + gap, 3, v.max.y - c.y - gap + 1),
                ftk::Box2I(v.min.x, c.y - 1, c.x - gap - v.min.x, 3),
                ftk::Box2I(c.x + gap, c.y - 1, v.max.x - c.x - gap + 1, 3)
            };
            event.render->drawRects(darkLines, dark);
            std::vector<ftk::Box2I> lightLines =
            {
                ftk::Box2I(c.x, v.min.y, 1, c.y - gap - v.min.y),
                ftk::Box2I(c.x, c.y + gap, 1, v.max.y - c.y - gap + 1),
                ftk::Box2I(v.min.x, c.y, c.x - gap - v.min.x, 1),
                ftk::Box2I(c.x + gap, c.y, v.max.x - c.x - gap + 1, 1)
            };
            event.render->drawRects(lightLines, light);
        }

        void PixelLoupe::_viewUpdate()
        {
            FTK_P();
            const ftk::Box2I& g = p.viewport->getGeometry();
            // The target pixel's block in the middle of the view: the pixel
            // at render x starts at viewPos + x * zoom.
            const ftk::V2I c(g.w() / 2, g.h() / 2);
            const ftk::V2I viewPos(
                c.x - p.target.x * zoom - zoom / 2,
                c.y - p.target.y * zoom - zoom / 2);
            p.viewport->setViewPosAndZoom(viewPos, zoom);
        }
    }
}

// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the DJV project.

#include <djv/App/ToolsWidget.h>

#include <djv/App/App.h>
#include <djv/App/IToolWidget.h>
#include <djv/Models/ToolsModel.h>

#include <algorithm>
#include <optional>

#include <ftk/UI/RowLayout.h>
#include <ftk/Core/FontSystem.h>
#include <ftk/UI/ScrollWidget.h>

namespace djv
{
    namespace app
    {
        struct ToolsWidget::Private
        {
            std::weak_ptr<App> app;
            std::weak_ptr<MainWindow> mainWindow;
            bool left = false;
            std::shared_ptr<ToolWidgetCache> cache;
            std::shared_ptr<ftk::VerticalLayout> layout;
            std::shared_ptr<ftk::ScrollWidget> scrollWidget;
            std::shared_ptr<ftk::ListObserver<std::string> > openObserver;
            std::shared_ptr<ftk::ListObserver<std::string> > leftObserver;
            std::shared_ptr<ftk::Observer<std::pair<std::string, std::string> > > showSectionObserver;

            // The section to scroll to, and the ticks left to wait for the
            // layout to place it.
            std::optional<std::pair<std::string, std::string> > scrollSection;
            int scrollTicks = 0;
        };

        void ToolsWidget::_init(
            const std::shared_ptr<ftk::Context>& context,
            const std::shared_ptr<App>& app,
            const std::shared_ptr<MainWindow>& mainWindow,
            bool left,
            const std::shared_ptr<ToolWidgetCache>& cache,
            const std::shared_ptr<IWidget>& parent)
        {
            IWidget::_init(
                context,
                "djv::app::ToolsWidget",
                parent);
            FTK_P();

            p.app = app;
            p.mainWindow = mainWindow;
            p.left = left;
            p.cache = cache;

            p.layout = ftk::VerticalLayout::create(context);
            p.layout->setMarginRole(ftk::SizeRole::None);
            p.layout->setSpacingRole(ftk::SizeRole::Border);

            // One scroll area for the whole stack rather than one inside each
            // tool: a tool then takes the height its contents need instead of
            // an equal share of the panel, and there is a single scroll bar
            // rather than one nested in another. Tools whose contents have no
            // natural end -- the logs -- keep their own, which stops at the
            // scroll area size role rather than growing without limit.
            p.scrollWidget = ftk::ScrollWidget::create(
                context, ftk::ScrollType::Both);
            _setWidget(p.scrollWidget);
            p.scrollWidget->setBorder(false);
            // Reordering the file list by dragging needs the list to move
            // when the drag reaches the edge of the panel, or a drop target
            // off the screen cannot be reached.
            p.scrollWidget->setDragScroll(true);
            p.scrollWidget->setWidget(p.layout);

            p.openObserver = ftk::ListObserver<std::string>::create(
                app->getToolsModel()->observeOpenTools(),
                [this](const std::vector<std::string>&)
                {
                    _widgetUpdate();
                });
            p.leftObserver = ftk::ListObserver<std::string>::create(
                app->getToolsModel()->observeLeftTools(),
                [this](const std::vector<std::string>&)
                {
                    _widgetUpdate();
                });

            // The tool is opened before the section is asked for, so its
            // widget has already been made by the observer above.
            p.showSectionObserver = ftk::Observer<std::pair<std::string, std::string> >::create(
                app->getToolsModel()->observeShowSection(),
                [this](const std::pair<std::string, std::string>& value)
                {
                    if (auto toolWidget = getToolWidget(value.first))
                    {
                        toolWidget->openSection(value.second);
                        // A tool just opened, or a section just expanded,
                        // has not been laid out yet; two ticks leave room
                        // for a layout between the request and the scroll
                        // whichever comes first in the frame.
                        _p->scrollSection = value;
                        _p->scrollTicks = 2;
                    }
                },
                ftk::ObserverAction::Suppress);
        }

        ToolsWidget::ToolsWidget() :
            _p(new Private)
        {}

        ToolsWidget::~ToolsWidget()
        {}

        std::shared_ptr<ToolsWidget> ToolsWidget::create(
            const std::shared_ptr<ftk::Context>& context,
            const std::shared_ptr<App>& app,
            const std::shared_ptr<MainWindow>& mainWindow,
            bool left,
            const std::shared_ptr<ToolWidgetCache>& cache,
            const std::shared_ptr<IWidget>& parent)
        {
            auto out = std::shared_ptr<ToolsWidget>(new ToolsWidget);
            out->_init(context, app, mainWindow, left, cache, parent);
            return out;
        }

        std::shared_ptr<IToolWidget> ToolsWidget::getToolWidget(
            const std::string& name) const
        {
            FTK_P();
            // Only an open tool, and only one docked here.
            auto app = p.app.lock();
            if (!app ||
                !app->getToolsModel()->isToolOpen(name) ||
                app->getToolsModel()->isToolLeft(name) != p.left)
            {
                return nullptr;
            }
            const auto i = p.cache->widgets.find(name);
            return i != p.cache->widgets.end() ? i->second : nullptr;
        }

        bool ToolsWidget::hasTools() const
        {
            FTK_P();
            if (auto app = p.app.lock())
            {
                const auto model = app->getToolsModel();
                for (const auto& name : model->getOpenTools())
                {
                    if (model->isToolLeft(name) == p.left)
                    {
                        return true;
                    }
                }
            }
            return false;
        }

        void ToolsWidget::tickEvent(
            bool parentsVisible,
            bool parentsEnabled,
            const ftk::TickEvent& event)
        {
            IContainer::tickEvent(parentsVisible, parentsEnabled, event);
            FTK_P();
            if (p.scrollSection.has_value())
            {
                if (--p.scrollTicks <= 0)
                {
                    if (auto toolWidget = getToolWidget(p.scrollSection->first))
                    {
                        // In the scrolled content's coordinates rather than
                        // the window's.
                        const ftk::Box2I g = ftk::move(
                            toolWidget->getSectionGeometry(p.scrollSection->second),
                            -p.layout->getGeometry().min);
                        p.scrollWidget->scrollTo(g);
                    }
                    p.scrollSection.reset();
                }
            }
        }

        void ToolsWidget::drawOverlayEvent(const ftk::Box2I& drawRect, const ftk::DrawEvent& event)
        {
            IContainer::drawOverlayEvent(drawRect, event);
            FTK_P();
            // The header of a tool being dragged to another place, held
            // under the pointer, over a shade where the tool is now.
            for (const auto& child : p.layout->getChildren())
            {
                auto tool = std::dynamic_pointer_cast<IToolWidget>(child);
                if (!tool)
                    continue;
                const auto header = tool->getDragHeader();
                if (!header.has_value())
                    continue;
                const ftk::Box2I& g = getGeometry();
                const ftk::Box2I place = tool->getHeaderGeometry();
                ftk::Color4F shade = event.style->getColorRole(ftk::ColorRole::Base);
                shade.a = .75F;
                event.render->drawRect(place, shade);

                ftk::Box2I box = header.value();
                box = ftk::Box2I(
                    box.min.x,
                    std::clamp(box.min.y, g.min.y, std::max(g.min.y, g.max.y - box.h() + 1)),
                    box.w(),
                    box.h());
                const int border = std::max(1, static_cast<int>(event.displayScale));
                event.render->drawRect(box, event.style->getColorRole(ftk::ColorRole::KeyFocus));
                event.render->drawRect(
                    ftk::margin(box, -border * 2),
                    event.style->getColorRole(ftk::ColorRole::Header));
                const ftk::FontInfo fontInfo = event.style->getFont(ftk::FontType::Regular, event.displayScale);
                const ftk::FontMetrics fontMetrics = event.fontSystem->getMetrics(fontInfo);
                const int margin = event.style->getSizeRole(ftk::SizeRole::MarginInside, event.displayScale);
                const int icon = event.style->getSizeRole(ftk::SizeRole::Icon, event.displayScale);
                event.render->drawText(
                    event.fontSystem->getGlyphs(tool->getToolName(), fontInfo),
                    fontMetrics,
                    ftk::V2I(
                        box.min.x + margin * 3 + icon * 2,
                        box.min.y + (box.h() - fontMetrics.lineHeight) / 2),
                    event.style->getColorRole(ftk::ColorRole::Text));
                break;
            }
        }

        void ToolsWidget::_widgetUpdate()
        {
            FTK_P();
            auto context = getContext();
            auto app = p.app.lock();
            auto mainWindow = p.mainWindow.lock();
            if (!context || !app)
                return;
            const auto model = app->getToolsModel();
            const auto& open = model->getOpenTools();

            // Out of this dock what is closed or docked on the other side.
            // Kept, not taken apart: opened again it is as it was left.
            for (const auto& child : p.layout->getChildren())
            {
                if (auto tool = std::dynamic_pointer_cast<IToolWidget>(child))
                {
                    const std::string& name = tool->getToolName();
                    if (std::find(open.begin(), open.end(), name) == open.end() ||
                        model->isToolLeft(name) != p.left)
                    {
                        tool->setParent(nullptr);
                    }
                }
            }

            size_t index = 0;
            for (const auto& name : open)
            {
                if (model->isToolLeft(name) != p.left)
                    continue;
                auto i = p.cache->widgets.find(name);
                if (i == p.cache->widgets.end())
                {
                    i = p.cache->widgets.insert(std::make_pair(
                        name,
                        app->getToolWidgetFactory()->createTool(
                            name, context, app, mainWindow, nullptr))).first;
                }
                // In the order of the list rather than the order the widgets
                // were made in: moved where it goes rather than taken out and
                // put back, so a tool being dragged into place keeps the
                // drag.
                if (i->second)
                {
                    if (i->second->getParent() != p.layout)
                    {
                        i->second->setParent(p.layout);
                    }
                    p.layout->moveToIndex(i->second, index);
                    ++index;
                }
            }
        }
    }
}

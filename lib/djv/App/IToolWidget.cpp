// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the DJV project.

#include <djv/App/IToolWidget.h>

#include <djv/App/App.h>

#include <ftk/UI/Icon.h>
#include <ftk/UI/Label.h>
#include <ftk/UI/ToolButton.h>
#include <ftk/UI/RowLayout.h>
#include <ftk/UI/Bellows.h>
#include <ftk/UI/Settings.h>
#include <ftk/Core/Format.h>

#include <algorithm>
#include <cmath>

namespace djv
{
    namespace app
    {
        namespace
        {
            //! The grip a tool is dragged by to another place in the stack:
            //! two columns of dots, as a panel's in Blender.
            class Grip : public ftk::IWidget
            {
            protected:
                void _init(const std::shared_ptr<ftk::Context>& context)
                {
                    IWidget::_init(context, "djv::app::Grip", nullptr);
                }

            public:
                static std::shared_ptr<Grip> create(const std::shared_ptr<ftk::Context>& context)
                {
                    auto out = std::shared_ptr<Grip>(new Grip);
                    out->_init(context);
                    return out;
                }

                ftk::Size2I getSizeHint() const override
                {
                    return _sizeHint;
                }

                void sizeHintEvent(const ftk::SizeHintEvent& event) override
                {
                    IWidget::sizeHintEvent(event);
                    const int icon = event.style->getSizeRole(ftk::SizeRole::Icon, event.displayScale);
                    const int margin = event.style->getSizeRole(ftk::SizeRole::MarginInside, event.displayScale);
                    _sizeHint = ftk::Size2I(icon + margin * 2, icon + margin * 2);
                }

                void drawEvent(const ftk::Box2I& drawRect, const ftk::DrawEvent& event) override
                {
                    IWidget::drawEvent(drawRect, event);
                    const ftk::Box2I& g = getGeometry();
                    const int dot = std::max(2, static_cast<int>(std::lround(2.F * event.displayScale)));
                    const int step = std::max(dot + 2, static_cast<int>(std::lround(5.F * event.displayScale)));
                    const int w = step + dot;
                    const int h = step * 2 + dot;
                    const int x0 = g.min.x + (g.w() - w) / 2;
                    const int y0 = g.min.y + (g.h() - h) / 2;
                    ftk::Color4F color = event.style->getColorRole(ftk::ColorRole::Text);
                    color.a = .6F;
                    std::vector<ftk::Box2I> boxes;
                    for (int y = 0; y < 3; ++y)
                    {
                        for (int x = 0; x < 2; ++x)
                        {
                            boxes.push_back(ftk::Box2I(x0 + x * step, y0 + y * step, dot, dot));
                        }
                    }
                    event.render->drawRects(boxes, color);
                }

            private:
                ftk::Size2I _sizeHint;
            };
        }

        struct IToolWidget::Private
        {
            std::shared_ptr<ftk::Settings> settings;
            std::string name;
            // Weak, because each bellows holds the callback that reads this
            // map: a shared pointer here would be a cycle.
            std::map<std::string, std::weak_ptr<ftk::Bellows> > bellows;
            
            std::shared_ptr<ftk::Icon> collapseIcon;
            std::shared_ptr<ftk::Icon> icon;
            std::shared_ptr<ftk::Label> label;
            std::shared_ptr<ftk::ToolButton> sideButton;
            std::shared_ptr<Grip> grip;
            //! Dragged by the grip to another place in the stack. Until the
            //! layout has put the tool where it went, its place is not
            //! looked at again, or it would go on past the tool it passed.
            bool reorderDrag = false;
            bool reorderPending = false;
            //! Where the pointer is, and how far below the top of the header
            //! it took hold of it.
            int dragY = 0;
            int dragGrabY = 0;
            std::shared_ptr<ftk::HorizontalLayout> headerLayout;
            bool collapsed = false;
            //! A press on the header toggles the tool, and a drag from it
            //! gives each header it crosses the same state, as dragging
            //! over the arrows of panels in Blender.
            bool headerPressed = false;
            bool headerDragCollapsed = false;
            std::shared_ptr<ftk::ListObserver<std::string> > leftObserver;
            std::shared_ptr<ftk::VerticalLayout> toolLayout;
            std::shared_ptr<ftk::VerticalLayout> layout;
        };

        void IToolWidget::_init(
            const std::shared_ptr<ftk::Context>& context,
            const std::shared_ptr<App>& app,
            const std::shared_ptr<MainWindow>& mainWindow,
            const std::string& name,
            const std::string& icon,
            const std::string& objectName,
            const std::shared_ptr<IWidget>& parent)
        {
            IWidget::_init(context, objectName, parent);
            FTK_P();

            _app = app;
            _mainWindow = mainWindow;

            p.settings = app->getSettings();
            p.name = name;

            p.icon = ftk::Icon::create(context, icon);

            p.label = ftk::Label::create(context, name);
            p.label->setMarginRole(ftk::SizeRole::LabelPad, ftk::SizeRole::None);
            p.label->setHStretch(ftk::Stretch::Expanding);

            // Collapsed to its header, as a panel in Blender: by the arrow,
            // or a click on the header itself.
            p.collapseIcon = ftk::Icon::create(context, "BellowsOpen");
            p.collapseIcon->setTooltip(
                "Click the header to collapse the tool to it, or to open it again. "
                "Drag across other headers to do the same to them.");

            // Docked on the other side of the viewport.
            p.sideButton = ftk::ToolButton::create(context);

            // No close button: a tool is closed from the Tools menu, the
            // tool bar or its shortcut, and opened again as it was left.
            p.grip = Grip::create(context);
            p.grip->setTooltip(
                "Drag up or down to put the tool somewhere else in the stack.");

            p.layout = ftk::VerticalLayout::create(context, shared_from_this());
            p.layout->setSpacingRole(ftk::SizeRole::Border);
            auto hLayout = ftk::HorizontalLayout::create(context, p.layout);
            hLayout->setMarginRole(ftk::SizeRole::MarginInside);
            hLayout->setSpacingRole(ftk::SizeRole::None);
            // Coloured so that each tool reads as its own thing in a stack of
            // them, rather than the panel being one long column of controls.
            hLayout->setBackgroundRole(ftk::ColorRole::Header);
            p.headerLayout = hLayout;
            p.collapseIcon->setParent(hLayout);
            p.icon->setParent(hLayout);
            p.label->setParent(hLayout);
            p.sideButton->setParent(hLayout);
            p.grip->setParent(hLayout);
            p.toolLayout = ftk::VerticalLayout::create(context, p.layout);
            p.toolLayout->setSpacingRole(ftk::SizeRole::None);
            p.toolLayout->setHStretch(ftk::Stretch::Expanding);
            p.toolLayout->setVStretch(ftk::Stretch::Expanding);

            // Closes this tool rather than whatever is open: more than one
            // can be, and closing is how a tool is put away.
            auto appWeak = std::weak_ptr<App>(app);

            p.sideButton->setClickedCallback(
                [appWeak, name]
                {
                    if (auto app = appWeak.lock())
                    {
                        auto model = app->getToolsModel();
                        model->setToolLeft(name, !model->isToolLeft(name));
                    }
                });
            p.leftObserver = ftk::ListObserver<std::string>::create(
                app->getToolsModel()->observeLeftTools(),
                [this, name](const std::vector<std::string>& value)
                {
                    const bool left = std::find(value.begin(), value.end(), name) != value.end();
                    _p->sideButton->setIcon(left ? "PanelRight" : "PanelLeft");
                    _p->sideButton->setTooltip(left ?
                        "Dock the tool on the right of the viewport." :
                        "Dock the tool on the left of the viewport.");
                });

            // Collapsed or not as it was last.
            bool collapsed = false;
            p.settings->get(ftk::Format("/{0}/Collapsed").arg(name), collapsed);
            setCollapsed(collapsed);
        }

        IToolWidget::IToolWidget() :
            _p(new Private)
        {}

        IToolWidget::~IToolWidget()
        {}

        const std::string& IToolWidget::getToolName() const
        {
            return _p->name;
        }

        void IToolWidget::scrollTo(const std::string&)
        {}

        void IToolWidget::openSection(const std::string& section)
        {
            FTK_P();
            if (const auto i = p.bellows.find(section); i != p.bellows.end())
            {
                if (auto bellows = i->second.lock())
                {
                    bellows->setOpen(true);
                    scrollTo(section);
                }
            }
        }

        ftk::Box2I IToolWidget::getSectionGeometry(const std::string& section) const
        {
            FTK_P();
            if (const auto i = p.bellows.find(section); i != p.bellows.end())
            {
                if (auto bellows = i->second.lock())
                {
                    return bellows->getGeometry();
                }
            }
            return getGeometry();
        }

        std::optional<ftk::Box2I> IToolWidget::getDragHeader() const
        {
            FTK_P();
            if (!p.reorderDrag)
                return std::nullopt;
            const ftk::Box2I& g = p.headerLayout->getGeometry();
            return ftk::Box2I(g.min.x, p.dragY - p.dragGrabY, g.w(), g.h());
        }

        ftk::Box2I IToolWidget::getHeaderGeometry() const
        {
            return _p->headerLayout->getGeometry();
        }

        bool IToolWidget::isCollapsed() const
        {
            return _p->collapsed;
        }

        void IToolWidget::setCollapsed(bool value)
        {
            FTK_P();
            p.collapsed = value;
            p.collapseIcon->setIcon(value ? "BellowsClosed" : "BellowsOpen");
            // Collapsed, the tool is its header alone. Open, it takes the
            // height its contents need, as it did before it could collapse:
            // stretched, the one tool open took the whole height of the
            // dock.
            p.toolLayout->setVisible(!value);
            p.settings->set(ftk::Format("/{0}/Collapsed").arg(p.name), value);
            setSizeUpdate();
            setDrawUpdate();
        }

        ftk::Size2I IToolWidget::getSizeHint() const
        {
            return _p->layout->getSizeHint();
        }

        void IToolWidget::mousePressEvent(ftk::MouseClickEvent& event)
        {
            IWidget::mousePressEvent(event);
            FTK_P();
            // A click on the header, not on one of its buttons, which take
            // their own.
            if (ftk::MouseButton::Left == event.button &&
                !event.accept &&
                ftk::contains(p.grip->getGeometry(), event.pos))
            {
                event.accept = true;
                p.reorderDrag = true;
                p.reorderPending = false;
                p.dragY = event.pos.y;
                p.dragGrabY = event.pos.y - p.headerLayout->getGeometry().min.y;
                setDrawUpdate();
                return;
            }
            if (ftk::MouseButton::Left == event.button &&
                !event.accept &&
                ftk::contains(p.headerLayout->getGeometry(), event.pos))
            {
                event.accept = true;
                p.headerPressed = true;
                setCollapsed(!p.collapsed);
                p.headerDragCollapsed = p.collapsed;
            }
        }

        void IToolWidget::mouseMoveEvent(ftk::MouseMoveEvent& event)
        {
            IWidget::mouseMoveEvent(event);
            FTK_P();
            if (p.reorderDrag)
            {
                event.accept = true;
                p.dragY = event.pos.y;
                setDrawUpdate();
                auto app = _app.lock();
                auto parent = getParent();
                if (p.reorderPending || !app || !parent)
                    return;
                std::vector<std::shared_ptr<IToolWidget> > tools;
                int index = -1;
                for (const auto& child : parent->getChildren())
                {
                    if (auto tool = std::dynamic_pointer_cast<IToolWidget>(child))
                    {
                        if (tool.get() == this)
                        {
                            index = static_cast<int>(tools.size());
                        }
                        tools.push_back(tool);
                    }
                }
                if (index < 0)
                    return;
                std::string other;
                bool after = false;
                if (index > 0 &&
                    event.pos.y < ftk::center(tools[index - 1]->getGeometry()).y)
                {
                    other = tools[index - 1]->getToolName();
                }
                else if (index + 1 < static_cast<int>(tools.size()) &&
                    event.pos.y > ftk::center(tools[index + 1]->getGeometry()).y)
                {
                    other = tools[index + 1]->getToolName();
                    after = true;
                }
                if (!other.empty())
                {
                    auto model = app->getToolsModel();
                    std::vector<std::string> order = model->getToolOrder();
                    order.erase(std::remove(order.begin(), order.end(), p.name), order.end());
                    auto i = std::find(order.begin(), order.end(), other);
                    if (i != order.end())
                    {
                        order.insert(after ? i + 1 : i, p.name);
                        p.reorderPending = true;
                        model->setToolOrder(order);
                    }
                }
                return;
            }
            if (p.headerPressed)
            {
                // The other tools of the dock whose header is under the
                // pointer.
                event.accept = true;
                if (auto parent = getParent())
                {
                    for (const auto& child : parent->getChildren())
                    {
                        auto tool = std::dynamic_pointer_cast<IToolWidget>(child);
                        if (tool &&
                            tool.get() != this &&
                            tool->isCollapsed() != p.headerDragCollapsed &&
                            ftk::contains(tool->_p->headerLayout->getGeometry(), event.pos))
                        {
                            tool->setCollapsed(p.headerDragCollapsed);
                        }
                    }
                }
            }
        }

        void IToolWidget::mouseReleaseEvent(ftk::MouseClickEvent& event)
        {
            IWidget::mouseReleaseEvent(event);
            FTK_P();
            if (p.headerPressed)
            {
                p.headerPressed = false;
                event.accept = true;
            }
            if (p.reorderDrag)
            {
                p.reorderDrag = false;
                p.reorderPending = false;
                event.accept = true;
                setDrawUpdate();
            }
        }

        void IToolWidget::setGeometry(const ftk::Box2I & value)
        {
            IWidget::setGeometry(value);
            _p->layout->setGeometry(value);
            // Laid out where the drag put it.
            _p->reorderPending = false;
        }

        void IToolWidget::_loadSettings(const std::map<std::string, std::shared_ptr<ftk::Bellows> >& value)
        {
            FTK_P();
            nlohmann::json json;
            p.settings->get(ftk::Format("/{0}/Bellows").arg(p.name), json);
            for (auto i = json.begin(); i != json.end(); ++i)
            {
                auto j = value.find(i.key());
                if (j != value.end() && i.value().is_boolean())
                {
                    j->second->setOpen(i.value().get<bool>());
                }
            }

            // The state is written when it changes; the write in the tool's
            // destructor is a backstop.
            p.bellows.clear();
            for (const auto& i : value)
            {
                p.bellows[i.first] = i.second;
                i.second->setOpenCallback(
                    [this](bool)
                    {
                        _bellowsSave();
                    });
            }
        }

        void IToolWidget::_bellowsSave()
        {
            FTK_P();
            nlohmann::json json;
            for (const auto& i : p.bellows)
            {
                if (auto bellows = i.second.lock())
                {
                    json[i.first] = bellows->isOpen();
                }
            }
            p.settings->set(ftk::Format("/{0}/Bellows").arg(p.name), json);
        }

        void IToolWidget::_saveSettings(const std::map<std::string, std::shared_ptr<ftk::Bellows> >& value)
        {
            FTK_P();
            nlohmann::json json;
            for (const auto& i : value)
            {
                json[i.first] = i.second->isOpen();
            }
            p.settings->set(ftk::Format("/{0}/Bellows").arg(p.name), json);
        }

        void IToolWidget::_setWidget(const std::shared_ptr<ftk::IWidget>& value)
        {
            value->setHStretch(ftk::Stretch::Expanding);
            value->setVStretch(ftk::Stretch::Expanding);
            value->setParent(_p->toolLayout);
        }

        struct ToolWidgetFactory::Private
        {
            std::map<std::string, ToolWidgetFnc> fncs;
        };

        ToolWidgetFactory::ToolWidgetFactory() :
            _p(new Private)
        {}

        ToolWidgetFactory::~ToolWidgetFactory()
        {}

        std::shared_ptr<ToolWidgetFactory> ToolWidgetFactory::create()
        {
            return std::shared_ptr<ToolWidgetFactory>(new ToolWidgetFactory);
        }

        void ToolWidgetFactory::addTool(
            const std::string& name,
            const ToolWidgetFnc& fnc)
        {
            _p->fncs[name] = fnc;
        }

        std::shared_ptr<IToolWidget> ToolWidgetFactory::createTool(
            const std::string& name,
            const std::shared_ptr<ftk::Context>& context,
            const std::shared_ptr<App>& app,
            const std::shared_ptr<MainWindow>& mainWindow,
            const std::shared_ptr<ftk::IWidget>& parent)
        {
            FTK_P();
            std::shared_ptr<IToolWidget> out;
            if (const auto i = p.fncs.find(name); i != p.fncs.end())
            {
                out = i->second(context, app, mainWindow, parent);
            }
            return out;
        }
    }
}

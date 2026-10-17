// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the DJV project.

#pragma once

#include <djv/App/Export.h>
#include <djv/Models/Export.h>

#include <ftk/UI/IContainer.h>

#include <map>

namespace djv
{
    namespace app
    {
        class App;
        class IToolWidget;
        class MainWindow;

        //! The tool widgets, made the first time each is opened and kept
        //! from then on: a tool closed and opened again is as it was left,
        //! its sections, its choices and its scrolling. Shared by the docks
        //! on both sides, so a tool moved across is the same widget.
        struct ToolWidgetCache
        {
            std::map<std::string, std::shared_ptr<IToolWidget> > widgets;
        };

        //! Tools widget: the tools docked on one side of the viewport.
        class DJV_APP_API_TYPE ToolsWidget : public ftk::IContainer
        {
            FTK_NON_COPYABLE(ToolsWidget);

        protected:
            void _init(
                const std::shared_ptr<ftk::Context>&,
                const std::shared_ptr<App>&,
                const std::shared_ptr<MainWindow>&,
                bool left,
                const std::shared_ptr<ToolWidgetCache>&,
                const std::shared_ptr<IWidget>& parent);

            ToolsWidget();

        public:
            DJV_APP_API virtual ~ToolsWidget();

            //! Create a new widget.
            DJV_APP_API static std::shared_ptr<ToolsWidget> create(
                const std::shared_ptr<ftk::Context>&,
                const std::shared_ptr<App>&,
                const std::shared_ptr<MainWindow>&,
                bool left,
                const std::shared_ptr<ToolWidgetCache>&,
                const std::shared_ptr<IWidget>& parent = nullptr);

            //! Get whether any open tool is docked here.
            DJV_APP_API bool hasTools() const;

            //! Get the active tool widget, or null if no tool is active.
            //! Get an open tool by name, or null when it is not open.
            DJV_APP_API std::shared_ptr<IToolWidget> getToolWidget(const std::string&) const;

            DJV_APP_API void tickEvent(bool, bool, const ftk::TickEvent&) override;

        private:
            void _widgetUpdate();

            FTK_PRIVATE();
        };
    }
}

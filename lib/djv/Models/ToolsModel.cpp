// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the DJV project.

#include <djv/Models/ToolsModel.h>
#include <djv/Models/SettingsKeys.h>

#include <ftk/UI/Settings.h>
#include <ftk/Core/Error.h>
#include <ftk/Core/String.h>

#include <algorithm>
#include <array>
#include <iostream>
#include <sstream>

namespace djv
{
    namespace models
    {
        struct ToolsModel::Private
        {
            std::shared_ptr<ftk::Settings> settings;
            std::vector<ToolInfo> tools;
            //! What the settings said was open, a tool an application adds
            //! later included.
            std::vector<std::string> openSaved;
            std::shared_ptr<ftk::ObservableList<std::string> > openTools;
            std::shared_ptr<ftk::ObservableList<std::string> > leftTools;
            std::vector<std::string> order;
            std::shared_ptr<ftk::Observable<std::pair<std::string, std::string> > > showSection;
        };

        void ToolsModel::_init(const std::shared_ptr<ftk::Settings>& settings)
        {
            FTK_P();

            p.settings = settings;

            p.tools.push_back({ "Files", "Files", "A", true, ftk::Key::F1 });
            p.tools.push_back({ "Layers", "Layers", "A1", true, ftk::KeyShortcut(ftk::Key::F7, ftk::KeyModifier::Shift) });
            p.tools.push_back({ "Export", "Export", "B", true, ftk::Key::F2 });
            p.tools.push_back({ "View", "View", "C", true, ftk::Key::F3 });
            p.tools.push_back({ "Color", "ColorControls", "D", true, ftk::Key::F4 });
            p.tools.push_back({ "Color Picker", "ColorPicker", "E", true, ftk::Key::F5 });
            p.tools.push_back({ "Magnify", "Magnify", "F", true, ftk::Key::F6 });
            p.tools.push_back({ "Scopes", "Scopes", "F1", true, ftk::KeyShortcut(ftk::Key::F6, ftk::KeyModifier::Shift) });
            p.tools.push_back({ "Information", "Info", "G", true, ftk::Key::F7 });
            p.tools.push_back({ "Audio", "Audio", "H", true, ftk::Key::F8 });
            p.tools.push_back({ "Review", "Review", "I", true, ftk::Key::F9 });
            p.tools.push_back({ "Settings", "Settings", "W", true, ftk::Key::F10 });
            p.tools.push_back({ "Messages", "Messages", "X", false, ftk::Key::F11 });
            p.tools.push_back({ "System Log", std::string(), "Y", false, ftk::Key::F12 });
            p.tools.push_back({ "Diagnostics", std::string(), "Z", false, ftk::KeyShortcut() });

            // More than one tool can be open now, so what was written before
            // says nothing about which; the key moves rather than trying to
            // read the old one as a list of one.
            p.settings->get(settingsKeys::toolsOpen, p.openSaved);
            p.settings->get(settingsKeys::toolsOrder, p.order);
            p.openTools = ftk::ObservableList<std::string>::create(_sorted(p.openSaved));
            std::vector<std::string> leftTools;
            p.settings->get(settingsKeys::toolsLeft, leftTools);
            p.leftTools = ftk::ObservableList<std::string>::create(leftTools);
            p.showSection = ftk::Observable<std::pair<std::string, std::string> >::create();
        }

        ToolsModel::ToolsModel() :
            _p(new Private)
        {}

        ToolsModel::~ToolsModel()
        {
            save();
        }

        void ToolsModel::save()
        {
            FTK_P();
            p.settings->set(settingsKeys::toolsOpen, p.openTools->get());
            p.settings->set(settingsKeys::toolsLeft, p.leftTools->get());
            p.settings->set(settingsKeys::toolsOrder, p.order);
        }

        std::shared_ptr<ToolsModel> ToolsModel::create(const std::shared_ptr<ftk::Settings>& settings)
        {
            auto out = std::shared_ptr<ToolsModel>(new ToolsModel);
            out->_init(settings);
            return out;
        }

        const std::vector<ToolInfo>& ToolsModel::getTools() const
        {
            return _p->tools;
        }
        
        void ToolsModel::addTool(const ToolInfo& value)
        {
            FTK_P();
            p.tools.push_back(value);
            // A tool that was open when the settings were saved is opened
            // again. The settings are read before an application has added
            // its tools, when the name meant nothing yet and was passed over.
            if (std::find(p.openSaved.begin(), p.openSaved.end(), value.name) !=
                p.openSaved.end())
            {
                setToolOpen(value.name, true);
            }
        }

        const std::vector<std::string>& ToolsModel::getOpenTools() const
        {
            return _p->openTools->get();
        }

        std::shared_ptr<ftk::IObservableList<std::string> > ToolsModel::observeOpenTools() const
        {
            return _p->openTools;
        }

        bool ToolsModel::isToolOpen(const std::string& value) const
        {
            FTK_P();
            const auto& open = p.openTools->get();
            return std::find(open.begin(), open.end(), value) != open.end();
        }

        void ToolsModel::setToolOpen(const std::string& value, bool open)
        {
            FTK_P();
            auto tools = p.openTools->get();
            const auto i = std::find(tools.begin(), tools.end(), value);
            if (open && i == tools.end())
            {
                tools.push_back(value);
            }
            else if (!open && i != tools.end())
            {
                tools.erase(i);
            }
            p.openTools->setIfChanged(_sorted(tools));
        }

        void ToolsModel::closeTools()
        {
            _p->openTools->setIfChanged(std::vector<std::string>());
        }

        void ToolsModel::showSection(const std::string& tool, const std::string& section)
        {
            FTK_P();
            setToolOpen(tool, true);
            // Always, since the same section is asked for again after the
            // user has closed it.
            p.showSection->setAlways(std::make_pair(tool, section));
        }

        std::shared_ptr<ftk::IObservable<std::pair<std::string, std::string> > > ToolsModel::observeShowSection() const
        {
            return _p->showSection;
        }

        bool ToolsModel::isToolLeft(const std::string& value) const
        {
            FTK_P();
            const auto& left = p.leftTools->get();
            return std::find(left.begin(), left.end(), value) != left.end();
        }

        void ToolsModel::setToolLeft(const std::string& value, bool left)
        {
            FTK_P();
            auto tools = p.leftTools->get();
            const auto i = std::find(tools.begin(), tools.end(), value);
            if (left && i == tools.end())
            {
                tools.push_back(value);
            }
            else if (!left && i != tools.end())
            {
                tools.erase(i);
            }
            if (p.leftTools->setIfChanged(tools))
            {
                p.settings->set(settingsKeys::toolsLeft, tools);
            }
        }

        std::shared_ptr<ftk::IObservableList<std::string> > ToolsModel::observeLeftTools() const
        {
            return _p->leftTools;
        }

        std::vector<std::string> ToolsModel::getToolOrder() const
        {
            FTK_P();
            // The order given, with what it does not name -- a tool added
            // since -- after it in the order the tools are listed, and what
            // is no tool any more left out.
            std::vector<std::string> out;
            for (const auto& name : p.order)
            {
                const bool known = std::find_if(
                    p.tools.begin(),
                    p.tools.end(),
                    [&name](const ToolInfo& tool) { return tool.name == name; }) != p.tools.end();
                if (known && std::find(out.begin(), out.end(), name) == out.end())
                {
                    out.push_back(name);
                }
            }
            for (const auto& tool : p.tools)
            {
                if (std::find(out.begin(), out.end(), tool.name) == out.end())
                {
                    out.push_back(tool.name);
                }
            }
            return out;
        }

        void ToolsModel::setToolOrder(const std::vector<std::string>& value)
        {
            FTK_P();
            if (value == p.order)
                return;
            p.order = value;
            p.settings->set(settingsKeys::toolsOrder, p.order);
            p.openTools->setIfChanged(_sorted(p.openTools->get()));
        }

        std::vector<std::string> ToolsModel::_sorted(
            const std::vector<std::string>& value) const
        {
            FTK_P();
            std::vector<std::string> out;
            for (const auto& name : getToolOrder())
            {
                if (std::find(value.begin(), value.end(), name) != value.end())
                {
                    out.push_back(name);
                }
            }
            return out;
        }
    }
}

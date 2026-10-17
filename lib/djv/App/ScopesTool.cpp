// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the DJV project.

#include <djv/App/ScopesTool.h>

#include <ftk/UI/Settings.h>

#include <djv/Models/SettingsKeys.h>

#include <djv/App/App.h>
#include <djv/App/MainWindow.h>
#include <djv/UI/ScopesWidget.h>

#include <tlRender/Timeline/Player.h>

#include <ftk/UI/ComboBox.h>
#include <ftk/UI/Label.h>
#include <ftk/UI/RowLayout.h>

namespace djv
{
    namespace app
    {
        struct ScopesTool::Private
        {
            std::shared_ptr<ftk::ComboBox> sourceComboBox;
            std::shared_ptr<ui::ScopesWidget> widget;
            std::shared_ptr<ftk::VerticalLayout> layout;

            std::shared_ptr<ftk::Observer<std::shared_ptr<tl::Player> > > playerObserver;
        };

        void ScopesTool::_init(
            const std::shared_ptr<ftk::Context>& context,
            const std::shared_ptr<App>& app,
            const std::shared_ptr<MainWindow>& mainWindow,
            const std::shared_ptr<IWidget>& parent)
        {
            IToolWidget::_init(
                context,
                app,
                mainWindow,
                "Scopes",
                "Scopes",
                "djv::app::ScopesTool",
                parent);
            FTK_P();

            p.sourceComboBox = ftk::ComboBox::create(
                context,
                std::vector<std::string>{ "Display (after color management)", "File values" });
            p.sourceComboBox->setHStretch(ftk::Stretch::Expanding);
            p.sourceComboBox->setTooltip(
                "Display: the picture as it is shown, after the OCIO view, "
                "as Blender's scopes measure it.\n"
                "File values: the values stored in the file, from zero to one.");

            p.widget = ui::ScopesWidget::create(context, app->getColorModel());
            // The heights the scopes were dragged to, kept from one session
            // to the next.
            std::weak_ptr<ftk::Settings> settingsWeak = app->getSettings();
            if (auto settings = app->getSettings())
            {
                nlohmann::json json;
                if (settings->get(models::settingsKeys::scopesHeights, json) &&
                    json.is_array() && 3 == json.size())
                {
                    try
                    {
                        std::array<float, 3> heights;
                        for (size_t i = 0; i < 3; ++i)
                        {
                            heights[i] = json[i].get<float>();
                        }
                        p.widget->setHeights(heights);
                    }
                    catch (const std::exception&)
                    {}
                }
            }
            if (auto settings = app->getSettings())
            {
                nlohmann::json json;
                if (settings->get(models::settingsKeys::scopesCollapsed, json) &&
                    json.is_array() && 3 == json.size())
                {
                    try
                    {
                        std::array<bool, 3> collapsed;
                        for (size_t i = 0; i < 3; ++i)
                        {
                            collapsed[i] = json[i].get<bool>();
                        }
                        p.widget->setCollapsed(collapsed);
                    }
                    catch (const std::exception&)
                    {}
                }
            }
            p.widget->setCollapsedCallback(
                [settingsWeak](const std::array<bool, 3>& value)
                {
                    if (auto settings = settingsWeak.lock())
                    {
                        settings->set(
                            models::settingsKeys::scopesCollapsed,
                            nlohmann::json(std::vector<bool>(value.begin(), value.end())));
                    }
                });
            p.widget->setHeightsCallback(
                [settingsWeak](const std::array<float, 3>& value)
                {
                    if (auto settings = settingsWeak.lock())
                    {
                        settings->set(
                            models::settingsKeys::scopesHeights,
                            nlohmann::json(std::vector<float>(value.begin(), value.end())));
                    }
                });

            p.layout = ftk::VerticalLayout::create(context);
            p.layout->setMarginRole(ftk::SizeRole::MarginSmall);
            p.layout->setSpacingRole(ftk::SizeRole::SpacingSmall);
            auto hLayout = ftk::HorizontalLayout::create(context, p.layout);
            hLayout->setSpacingRole(ftk::SizeRole::SpacingSmall);
            ftk::Label::create(context, "Measure:", hLayout);
            p.sourceComboBox->setParent(hLayout);
            p.widget->setParent(p.layout);
            _setWidget(p.layout);

            p.sourceComboBox->setIndexCallback(
                [this](int value)
                {
                    _p->widget->setSource(static_cast<ui::ScopesSource>(value));
                });

            p.playerObserver = ftk::Observer<std::shared_ptr<tl::Player> >::create(
                app->observePlayer(),
                [this](const std::shared_ptr<tl::Player>& value)
                {
                    _p->widget->setPlayer(value);
                });
        }

        ScopesTool::ScopesTool() :
            _p(new Private)
        {}

        ScopesTool::~ScopesTool()
        {}

        std::shared_ptr<ScopesTool> ScopesTool::create(
            const std::shared_ptr<ftk::Context>& context,
            const std::shared_ptr<App>& app,
            const std::shared_ptr<MainWindow>& mainWindow,
            const std::shared_ptr<IWidget>& parent)
        {
            auto out = std::shared_ptr<ScopesTool>(new ScopesTool);
            out->_init(context, app, mainWindow, parent);
            return out;
        }
    }
}

// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the DJV project.

#include <djv/App/ColorPickerTool.h>

#include <djv/App/App.h>
#include <djv/App/MainWindow.h>
#include <djv/UI/Viewport.h>
#include <djv/Models/SettingsModel.h>

#include <ftk/UI/ColorSwatch.h>
#include <ftk/UI/FormLayout.h>
#include <ftk/UI/Label.h>
#include <ftk/UI/RowLayout.h>
#include <ftk/UI/ScreenshotTag.h>
#include <ftk/Core/Format.h>

namespace djv
{
    namespace app
    {
        struct ColorPickerTool::Private
        {
            std::shared_ptr<ftk::ColorSwatch> colorSwatch;
            std::shared_ptr<ftk::Label> colorLabel;
            std::shared_ptr<ftk::Label> fileLabel;
            std::shared_ptr<ftk::Label> pixelLabel;
            std::shared_ptr<ftk::Label> mouseLabel;

            std::shared_ptr<ftk::Observer<std::optional<ui::PixelSample> > > pixelSampleObserver;
            std::shared_ptr<ftk::Observer<models::MouseSettings> > settingsObserver;
        };

        void ColorPickerTool::_init(
            const std::shared_ptr<ftk::Context>& context,
            const std::shared_ptr<App>& app,
            const std::shared_ptr<MainWindow>& mainWindow,
            const std::shared_ptr<IWidget>& parent)
        {
            IToolWidget::_init(
                context,
                app,
                mainWindow,
                "Color Picker",
                "ColorPicker",
                "djv::app::ColorPickerTool",
                parent);
            FTK_P();

            p.colorSwatch = ftk::ColorSwatch::create(context);
            p.colorSwatch->setColor(ftk::Color4F(0.F, 0.F, 0.F));
            p.colorSwatch->setBorder(false);
            p.colorSwatch->setSizeRole(ftk::SizeRole::SwatchLarge);

            p.colorLabel = ftk::Label::create(context);
            p.colorLabel->setFont(ftk::FontType::Mono);
            ftk::setScreenshotTag(p.colorLabel, "ColorPicker.Color");

            p.fileLabel = ftk::Label::create(context);
            p.fileLabel->setFont(ftk::FontType::Mono);
            p.fileLabel->setTooltip("The values stored in the file, before color management.");
            ftk::setScreenshotTag(p.fileLabel, "ColorPicker.File");

            p.pixelLabel = ftk::Label::create(context);
            p.pixelLabel->setFont(ftk::FontType::Mono);
            ftk::setScreenshotTag(p.pixelLabel, "ColorPicker.Pixel");

            p.mouseLabel = ftk::Label::create(context);
            ftk::setScreenshotTag(p.mouseLabel, "ColorPicker.Mouse");

            auto layout = ftk::VerticalLayout::create(context);
            layout->setSpacingRole(ftk::SizeRole::None);
            p.colorSwatch->setParent(layout);
            auto formLayout = ftk::FormLayout::create(context, layout);
            formLayout->setMarginRole(ftk::SizeRole::Margin);
            formLayout->setSpacingRole(ftk::SizeRole::SpacingSmall);
            formLayout->addRow("Displayed:", p.colorLabel);
            formLayout->addRow("File:", p.fileLabel);
            formLayout->addRow("Pixel:", p.pixelLabel);
            formLayout->addRow("Mouse:", p.mouseLabel);

            _setWidget(layout);

            // The probe's sample, the same as the HUD: the picture's pixel
            // as displayed, and as stored in the file.
            p.pixelSampleObserver = ftk::Observer<std::optional<ui::PixelSample> >::create(
                mainWindow->getViewport()->observePixelSample(),
                [this](const std::optional<ui::PixelSample>& value)
                {
                    FTK_P();
                    ftk::Color4F swatch;
                    std::string text = "-";
                    std::string fileText = "-";
                    std::string pixelText = "-";
                    if (value.has_value())
                    {
                        if (value->display.has_value())
                        {
                            const ftk::Color4F& c = value->display.value();
                            swatch = c;
                            text = ftk::Format("{0} {1} {2} {3}").
                                arg(c.r, 3).
                                arg(c.g, 3).
                                arg(c.b, 3).
                                arg(c.a, 3);
                        }
                        if (value->file.has_value())
                        {
                            const ftk::Color4F& c = value->file.value();
                            fileText = ftk::Format("{0} {1} {2} {3}").
                                arg(c.r, 4).
                                arg(c.g, 4).
                                arg(c.b, 4).
                                arg(c.a, 4);
                        }
                        pixelText = ftk::Format("{0}").arg(value->pixel);
                        if (value->average > 0)
                        {
                            const int n = value->average * 2 + 1;
                            pixelText += ftk::Format(" ({0}x{1})").arg(n).arg(n).str();
                        }
                    }
                    // Opaque: the swatch is what is on screen.
                    swatch.a = 1.F;
                    p.colorSwatch->setColor(swatch);
                    p.colorLabel->setText(text);
                    p.fileLabel->setText(fileText);
                    p.pixelLabel->setText(pixelText);
                });

            p.settingsObserver = ftk::Observer<models::MouseSettings>::create(
                app->getSettingsModel()->observeMouse(),
                [this](const models::MouseSettings& value)
                {
                    std::vector<std::string> s;
                    if (auto i = value.bindings.find(models::MouseAction::Pick);
                        i != value.bindings.end())
                    {
                        if (i->second.button != ftk::MouseButton::None)
                        {
                            if (i->second.modifier != ftk::KeyModifier::None)
                            {
                                s.push_back(ftk::to_string(i->second.modifier));
                            }
                            s.push_back(ftk::getLabel(i->second.button));
                        }
                    }
                    _p->mouseLabel->setText(
                        ftk::Format("{0} Click").arg(ftk::join(s, " + ")));
                });
        }

        ColorPickerTool::ColorPickerTool() :
            _p(new Private)
        {}

        ColorPickerTool::~ColorPickerTool()
        {}

        std::shared_ptr<ColorPickerTool> ColorPickerTool::create(
            const std::shared_ptr<ftk::Context>& context,
            const std::shared_ptr<App>& app,
            const std::shared_ptr<MainWindow>& mainWindow,
            const std::shared_ptr<IWidget>& parent)
        {
            auto out = std::shared_ptr<ColorPickerTool>(new ColorPickerTool);
            out->_init(context, app, mainWindow, parent);
            return out;
        }
    }
}

// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the DJV project.

#include <djv/App/LayersTool.h>

#include <djv/App/App.h>
#include <djv/App/MainWindow.h>
#include <djv/Models/ColorModel.h>
#include <djv/Models/LayerStack.h>
#include <djv/Models/SettingsModel.h>

#include <tlRender/UI/ThumbnailSystem.h>

#include <ftk/UI/ComboBox.h>
#include <ftk/UI/Divider.h>
#include <ftk/UI/IntEdit.h>
#include <ftk/UI/Label.h>
#include <ftk/UI/PushButton.h>
#include <ftk/UI/RowLayout.h>
#include <ftk/UI/ToolButton.h>
#include <ftk/Core/Format.h>
#include <ftk/Core/Timer.h>

namespace djv
{
    namespace app
    {
        struct LayersTool::Private
        {
            std::weak_ptr<App> app;
            std::optional<models::LayerStack> stack;
            //! A change made from a row's own widget, applied on the next
            //! tick: applying it rebuilds the rows, which would destroy the
            //! widget while its callback runs.
            std::optional<models::LayerStack> pending;
            std::shared_ptr<ftk::Timer> pendingTimer;

            std::shared_ptr<ftk::PushButton> newButton;
            std::shared_ptr<ftk::PushButton> addButton;
            std::shared_ptr<ftk::PushButton> saveButton;
            std::shared_ptr<ftk::Label> infoLabel;
            std::shared_ptr<ftk::VerticalLayout> rowsLayout;
            std::shared_ptr<ftk::VerticalLayout> layout;

            //! What each layer's file is, asked for once by path, and the
            //! line under each row that says it.
            std::map<std::string, tl::IOInfo> infos;
            std::map<std::string, tl::ui::InfoRequest> infoRequests;
            std::shared_ptr<ftk::Timer> infoTimer;
            std::vector<std::pair<int, std::shared_ptr<ftk::Label> > > infoLabels;

            std::shared_ptr<ftk::Observer<std::optional<models::LayerStack> > > stackObserver;
            std::shared_ptr<ftk::Observer<tl::OCIOOptions> > ocioObserver;
        };

        void LayersTool::_init(
            const std::shared_ptr<ftk::Context>& context,
            const std::shared_ptr<App>& app,
            const std::shared_ptr<MainWindow>& mainWindow,
            const std::shared_ptr<IWidget>& parent)
        {
            IToolWidget::_init(
                context,
                app,
                mainWindow,
                "Layers",
                "Layers",
                "djv::app::LayersTool",
                parent);
            FTK_P();
            p.app = app;
            p.pendingTimer = ftk::Timer::create(context);
            p.infoTimer = ftk::Timer::create(context);

            p.newButton = ftk::PushButton::create(context, "Stack Open Files");
            p.newButton->setIcon("FileNew");
            p.newButton->setTooltip(
                "Stack the open files as layers, in a new layer stack.\n"
                "Pictures without alpha (JPG, movies) go to the bottom, and "
                "every layer is placed by its own frame numbers.");
            p.addButton = ftk::PushButton::create(context, "Add Layers...");
            p.addButton->setIcon("Add");
            p.addButton->setTooltip(
                "Choose files to add on top of the current layer stack.\n"
                "Selecting every frame of a sequence adds it once.");

            p.saveButton = ftk::PushButton::create(context, "Save As...");
            p.saveButton->setIcon("FileSave");
            p.saveButton->setTooltip(
                "Save the layer stack where you want it, as an OpenTimelineIO "
                "file, to open again later.");

            p.infoLabel = ftk::Label::create(context);
            p.infoLabel->setMarginRole(ftk::SizeRole::MarginSmall);

            p.layout = ftk::VerticalLayout::create(context);
            p.layout->setMarginRole(ftk::SizeRole::MarginSmall);
            p.layout->setSpacingRole(ftk::SizeRole::SpacingSmall);
            auto buttonLayout = ftk::HorizontalLayout::create(context, p.layout);
            buttonLayout->setSpacingRole(ftk::SizeRole::SpacingSmall);
            p.newButton->setParent(buttonLayout);
            p.addButton->setParent(buttonLayout);
            p.saveButton->setParent(buttonLayout);
            p.infoLabel->setParent(p.layout);
            p.rowsLayout = ftk::VerticalLayout::create(context, p.layout);
            p.rowsLayout->setSpacingRole(ftk::SizeRole::SpacingSmall);
            _setWidget(p.layout);

            std::weak_ptr<App> appWeak(app);
            p.newButton->setClickedCallback(
                [appWeak]
                {
                    if (auto app = appWeak.lock())
                    {
                        app->newLayerStack();
                    }
                });
            p.addButton->setClickedCallback(
                [appWeak]
                {
                    if (auto app = appWeak.lock())
                    {
                        app->addLayersDialog();
                    }
                });

            p.saveButton->setClickedCallback(
                [appWeak]
                {
                    if (auto app = appWeak.lock())
                    {
                        app->saveLayerStackDialog();
                    }
                });

            p.stackObserver = ftk::Observer<std::optional<models::LayerStack> >::create(
                app->observeLayerStack(),
                [this](const std::optional<models::LayerStack>& value)
                {
                    _p->stack = value;
                    _rowsUpdate();
                });

            // The color spaces offered are the configuration's.
            p.ocioObserver = ftk::Observer<tl::OCIOOptions>::create(
                app->getColorModel()->observeOCIOOptions(),
                [this](const tl::OCIOOptions&)
                {
                    _rowsUpdate();
                });
        }

        LayersTool::LayersTool() :
            _p(new Private)
        {}

        LayersTool::~LayersTool()
        {}

        std::shared_ptr<LayersTool> LayersTool::create(
            const std::shared_ptr<ftk::Context>& context,
            const std::shared_ptr<App>& app,
            const std::shared_ptr<MainWindow>& mainWindow,
            const std::shared_ptr<IWidget>& parent)
        {
            auto out = std::shared_ptr<LayersTool>(new LayersTool);
            out->_init(context, app, mainWindow, parent);
            return out;
        }

        void LayersTool::_apply(const models::LayerStack& value)
        {
            FTK_P();
            p.pending = value;
            p.pendingTimer->start(
                std::chrono::milliseconds(0),
                [this]
                {
                    FTK_P();
                    if (p.pending.has_value())
                    {
                        const models::LayerStack stack = p.pending.value();
                        p.pending.reset();
                        if (auto app = p.app.lock())
                        {
                            app->setLayerStack(stack);
                        }
                    }
                });
        }

        void LayersTool::_infoUpdate()
        {
            FTK_P();
            auto app = p.app.lock();
            if (!app || !p.stack.has_value())
                return;

            // The answers that came.
            auto i = p.infoRequests.begin();
            while (i != p.infoRequests.end())
            {
                if (i->second.future.valid() &&
                    std::future_status::ready == i->second.future.wait_for(std::chrono::seconds(0)))
                {
                    p.infos[i->first] = i->second.future.get();
                    i = p.infoRequests.erase(i);
                }
                else
                {
                    ++i;
                }
            }

            const auto colorModel = app->getColorModel();
            for (const auto& row : p.infoLabels)
            {
                if (row.first < 0 || row.first >= static_cast<int>(p.stack->layers.size()))
                    continue;
                const models::Layer& layer = p.stack->layers[row.first];
                const auto j = p.infos.find(layer.path.get());
                if (j == p.infos.end())
                {
                    row.second->setText("Reading...");
                    row.second->setTooltip(std::string());
                    continue;
                }
                const tl::IOInfo& info = j->second;
                std::vector<std::string> parts;
                std::vector<std::string> lines;
                lines.push_back(layer.path.get());
                if (!info.video.empty())
                {
                    const ftk::ImageInfo& video = info.video.front();
                    parts.push_back(ftk::Format("{0}x{1}").arg(video.size.w).arg(video.size.h));
                    parts.push_back(ftk::to_string(video.type));
                    lines.push_back(ftk::Format("Size: {0}x{1}, pixel aspect {2}").
                        arg(video.size.w).arg(video.size.h).arg(video.pixelAspectRatio, 2));
                    lines.push_back("Pixels: " + ftk::to_string(video.type));
                }
                if (info.videoTime.has_value())
                {
                    const OTIO_NS::TimeRange& range = info.videoTime.value();
                    const int64_t first = static_cast<int64_t>(std::llround(range.start_time().value()));
                    const int64_t last = static_cast<int64_t>(std::llround(range.end_time_inclusive().value()));
                    std::string frames = ftk::Format("{0}-{1}").arg(first).arg(last);
                    if (layer.offset != 0)
                    {
                        // Where the offset puts it in the stack.
                        frames += ftk::Format(" (at {0}-{1})").
                            arg(first + layer.offset).
                            arg(last + layer.offset);
                    }
                    parts.push_back(frames);
                    lines.push_back(ftk::Format("Frames: {0}, {1} fps").
                        arg(frames).
                        arg(range.duration().rate(), 2));
                }
                // The color space the layer is read in: the one chosen, or
                // what the file and the rules make of it.
                std::string colorSpace;
                if (layer.colorSpace == models::getLayerViewInverseToken())
                {
                    colorSpace = "View inverse";
                }
                else if (!layer.colorSpace.empty())
                {
                    colorSpace = layer.colorSpace + " (chosen)";
                }
                else
                {
                    const std::string resolved = colorModel->resolveInput(layer.path.get(), info.tags);
                    colorSpace = resolved.empty() ?
                        std::string("the input of the color options") :
                        resolved + " (automatic)";
                }
                parts.push_back(colorSpace);
                lines.push_back("Color space: " + colorSpace);
                if (!info.tags.empty())
                {
                    lines.push_back(std::string());
                    size_t n = 0;
                    for (const auto& tag : info.tags)
                    {
                        if (++n > 24)
                        {
                            lines.push_back(ftk::Format("... and {0} more in the Information tool "
                                "with the layer opened by itself").arg(info.tags.size() - 24));
                            break;
                        }
                        std::string value = tag.second;
                        if (value.size() > 80)
                        {
                            value = value.substr(0, 77) + "...";
                        }
                        lines.push_back(tag.first + ": " + value);
                    }
                }
                row.second->setText(ftk::join(parts, "  \xC2\xB7  "));
                row.second->setTooltip(ftk::join(lines, '\n'));
            }

            // Looked for again until every answer came.
            if (!p.infoRequests.empty())
            {
                p.infoTimer->start(
                    std::chrono::milliseconds(100),
                    [this]
                    {
                        _infoUpdate();
                    });
            }
        }

        void LayersTool::_rowsUpdate()
        {
            FTK_P();
            auto context = getContext();
            auto app = p.app.lock();
            if (!context || !app)
                return;

            const auto children = p.rowsLayout->getChildren();
            for (const auto& child : children)
            {
                child->setParent(nullptr);
            }
            p.infoLabels.clear();

            p.saveButton->setEnabled(p.stack.has_value());
            if (!p.stack.has_value())
            {
                p.infoLabel->setText(
                    "The current file is not a layer stack.\n"
                    "Open the files to stack -- a render and its overlays -- "
                    "and click \"Stack Open Files\".");
                p.infoLabel->setVisible(true);
                return;
            }
            p.infoLabel->setText(ftk::Format("{0}: {1} layer(s), top first.").
                arg(p.stack->name).
                arg(p.stack->layers.size()));

            std::vector<std::string> colorSpaceItems =
            {
                "Automatic",
                "View inverse"
            };
            const std::vector<std::string> colorSpaces = app->getColorModel()->getColorSpaceNames();
            colorSpaceItems.insert(colorSpaceItems.end(), colorSpaces.begin(), colorSpaces.end());

            const models::LayerStack stack = p.stack.value();
            const int count = static_cast<int>(stack.layers.size());
            // Top first, as an editor shows its tracks.
            for (int i = count - 1; i >= 0; --i)
            {
                const models::Layer& layer = stack.layers[i];

                auto rowLayout = ftk::VerticalLayout::create(context, p.rowsLayout);
                rowLayout->setSpacingRole(ftk::SizeRole::SpacingSmall);
                rowLayout->setMarginRole(ftk::SizeRole::MarginInside);
                rowLayout->setBackgroundRole(ftk::ColorRole::Base);

                auto topLayout = ftk::HorizontalLayout::create(context, rowLayout);
                topLayout->setSpacingRole(ftk::SizeRole::SpacingSmall);
                auto visibleButton = ftk::ToolButton::create(context, topLayout);
                visibleButton->setIcon(layer.visible ? "LayerVisible" : "LayerHidden");
                visibleButton->setTooltip("Show or hide the layer.");
                auto nameLabel = ftk::Label::create(context, layer.name, topLayout);
                nameLabel->setHStretch(ftk::Stretch::Expanding);
                nameLabel->setElide(true, ftk::ElideMode::Middle);
                nameLabel->setTooltip(layer.path.get());
                auto upButton = ftk::ToolButton::create(context, topLayout);
                upButton->setIcon("ArrowUp");
                upButton->setTooltip("Move the layer up.");
                upButton->setEnabled(i < count - 1);
                auto downButton = ftk::ToolButton::create(context, topLayout);
                downButton->setIcon("ArrowDown");
                downButton->setTooltip("Move the layer down.");
                downButton->setEnabled(i > 0);
                auto removeButton = ftk::ToolButton::create(context, topLayout);
                removeButton->setIcon("CloseSmall");
                removeButton->setTooltip("Remove the layer from the stack.");
                removeButton->setEnabled(count > 1);

                auto bottomLayout = ftk::HorizontalLayout::create(context, rowLayout);
                bottomLayout->setSpacingRole(ftk::SizeRole::SpacingSmall);
                ftk::Label::create(context, "Offset:", bottomLayout);
                auto offsetEdit = ftk::IntEdit::create(context, bottomLayout);
                offsetEdit->setRange(-1000000, 1000000);
                offsetEdit->setValue(static_cast<int>(layer.offset));
                offsetEdit->setTooltip(
                    "Frames to move the layer by: later when positive, earlier "
                    "when negative.");
                auto colorSpaceComboBox = ftk::ComboBox::create(context, colorSpaceItems, bottomLayout);
                colorSpaceComboBox->setHStretch(ftk::Stretch::Expanding);
                colorSpaceComboBox->setTooltip(
                    "The layer's input color space.\n"
                    "Automatic: from the file, as for any file.\n"
                    "View inverse: for a picture saved with the view already "
                    "applied (a JPG or movie saved \"as render\"); it is taken "
                    "back through the inverse of the current view, so the view "
                    "gives it back as it was saved.");
                int csIndex = 0;
                if (layer.colorSpace == models::getLayerViewInverseToken())
                {
                    csIndex = 1;
                }
                else if (!layer.colorSpace.empty())
                {
                    const auto j = std::find(colorSpaces.begin(), colorSpaces.end(), layer.colorSpace);
                    if (j != colorSpaces.end())
                    {
                        csIndex = 2 + static_cast<int>(j - colorSpaces.begin());
                    }
                }
                colorSpaceComboBox->setCurrentIndex(csIndex);

                // What the layer's file is, in a line: the Information tool
                // is about the stack, which says nothing of its layers. The
                // rest, metadata and all, in the tooltip.
                auto infoLabel = ftk::Label::create(context, rowLayout);
                infoLabel->setTextRole(ftk::ColorRole::TextDisabled);
                infoLabel->setElide(true);
                p.infoLabels.push_back(std::make_pair(i, infoLabel));
                const std::string key = layer.path.get();
                if (p.infos.find(key) == p.infos.end() &&
                    p.infoRequests.find(key) == p.infoRequests.end())
                {
                    if (auto thumbnailSystem = context->getSystem<tl::ui::ThumbnailSystem>())
                    {
                        p.infoRequests[key] = thumbnailSystem->getInfo(
                            layer.path,
                            app->getSettingsModel()->getIOOptions());
                    }
                }

                visibleButton->setClickedCallback(
                    [this, stack, i]
                    {
                        models::LayerStack tmp = stack;
                        tmp.layers[i].visible = !tmp.layers[i].visible;
                        _apply(tmp);
                    });
                upButton->setClickedCallback(
                    [this, stack, i]
                    {
                        models::LayerStack tmp = stack;
                        std::swap(tmp.layers[i], tmp.layers[i + 1]);
                        _apply(tmp);
                    });
                downButton->setClickedCallback(
                    [this, stack, i]
                    {
                        models::LayerStack tmp = stack;
                        std::swap(tmp.layers[i], tmp.layers[i - 1]);
                        _apply(tmp);
                    });
                removeButton->setClickedCallback(
                    [this, stack, i]
                    {
                        models::LayerStack tmp = stack;
                        tmp.layers.erase(tmp.layers.begin() + i);
                        _apply(tmp);
                    });
                offsetEdit->setCallback(
                    [this, stack, i](int value)
                    {
                        models::LayerStack tmp = stack;
                        tmp.layers[i].offset = value;
                        _apply(tmp);
                    });
                colorSpaceComboBox->setIndexCallback(
                    [this, stack, i, colorSpaces](int value)
                    {
                        models::LayerStack tmp = stack;
                        if (0 == value)
                        {
                            tmp.layers[i].colorSpace = std::string();
                        }
                        else if (1 == value)
                        {
                            tmp.layers[i].colorSpace = models::getLayerViewInverseToken();
                        }
                        else if (value - 2 < static_cast<int>(colorSpaces.size()))
                        {
                            tmp.layers[i].colorSpace = colorSpaces[value - 2];
                        }
                        _apply(tmp);
                    });
            }

            _infoUpdate();
        }
    }
}

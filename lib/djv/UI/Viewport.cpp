// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the DJV project.

#include <djv/UI/Viewport.h>

#include <djv/UI/PixelLoupe.h>
#include <djv/UI/PixelProbe.h>
#include <djv/UI/RemoteSession.h>
#include <djv/UI/ViewportBinding.h>

#include <djv/Models/AnnotationsModel.h>
#include <djv/Models/ColorModel.h>
#include <djv/Models/DrawModel.h>
#include <djv/Models/FilesModel.h>
#include <djv/Models/SettingsModel.h>
#include <djv/Models/Stroke.h>
#include <djv/Models/TimeUnitsModel.h>
#include <djv/Models/ViewportModel.h>

#include <tlRender/Timeline/Util.h>

#include <ftk/UI/ColorSwatch.h>
#include <ftk/UI/IWindow.h>
#include <ftk/UI/Label.h>
#include <ftk/UI/Menu.h>
#include <ftk/UI/SysLogModel.h>
#include <ftk/UI/RowLayout.h>
#include <ftk/UI/ScreenshotTag.h>
#include <ftk/UI/Spacer.h>
#include <ftk/Core/Context.h>
#include <ftk/Core/Format.h>
#include <ftk/Core/LogSystem.h>
#include <ftk/Core/String.h>
#include <ftk/Core/Timer.h>

#if defined(FTK_SDL3)
#include <SDL3/SDL.h>
#endif // FTK_SDL3

#include <algorithm>
#include <chrono>

#include <cmath>
#include <regex>


namespace djv
{
    namespace ui
    {
        namespace
        {
            // Long enough for a short path, short enough that the message does
            // not run the width of the viewport. The untruncated text is in
            // the messages tool.
            const size_t toastTextLength = 80;

            const std::chrono::seconds toastTimeout(5);
            const std::chrono::seconds hintTimeout(3);
        }

        namespace
        {
            //! The mouse's own motion while probing, gathered from SDL's
            //! events as they come and taken by the next move or tick: the
            //! raw counts, each through the speed curve, in the viewport's
            //! pixels.
            struct ProbeMotion
            {
                float dx = 0.F;
                float dy = 0.F;
                uint64_t timestamp = 0;
                float speed = 0.F;
                //! Pixels a count moves at the slowest and the fastest.
                float minGain = 1.F;
                float maxGain = 1.F;
            };

    #if defined(FTK_SDL3)
            bool SDLCALL probeWatch(void* userdata, SDL_Event* event)
            {
                // Every motion of the mouse in relative mode: the raw counts,
                // each through an exponential curve on how fast the mouse is
                // going, so slow moves are fine and fast ones go far.
                if (SDL_EVENT_MOUSE_MOTION == event->type)
                {
                    auto motion = static_cast<ProbeMotion*>(userdata);
                    const float xrel = event->motion.xrel;
                    const float yrel = event->motion.yrel;
                    const float counts = std::sqrt(xrel * xrel + yrel * yrel);
                    const uint64_t timestamp = event->motion.timestamp;
                    float dt = .008F;
                    if (motion->timestamp > 0 && timestamp > motion->timestamp)
                    {
                        dt = std::max((timestamp - motion->timestamp) / 1000000000.F, .001F);
                    }
                    motion->timestamp = timestamp;
                    // Counts a second, smoothed so one quick event in a slow
                    // move does not jump.
                    motion->speed = motion->speed * .5F + (counts / dt) * .5F;
                    const float gain = std::clamp(
                        motion->minGain * std::exp(motion->speed / 900.F),
                        motion->minGain,
                        motion->maxGain);
                    motion->dx += xrel * gain;
                    motion->dy += yrel * gain;
                }
                return true;
            }
    #endif // FTK_SDL3
        }

        struct Viewport::Private
        {
            std::weak_ptr<models::ViewportModel> viewportModel;
            std::weak_ptr<models::TimeUnitsModel> timeUnitsModel;
            models::HUDOptions hudOptions;
            ftk::Path path;
            tl::IOInfo ioInfo;
            //! Which layer of the file is being shown. The HUD reports that
            //! layer, not the first one: a file can hold layers of different
            //! types.
            int videoLayer = 0;
            std::optional<OTIO_NS::RationalTime> currentTime;
            //! The player's time range, for the position counted from the
            //! start of the media rather than from frame zero.
            std::optional<OTIO_NS::TimeRange> timeRange;
            double fps = 0.0;
            size_t droppedFrames = 0;
            size_t videoFramesSize = 0;
            // Whether the picture stands in for a frame the media does not
            // have, and the frame it repeats when there is one.
            bool missing = false;
            std::optional<int64_t> heldFrom;
            // Kept for the annotation hit test and overlay, which need the
            // image sizes to map screen to image pixels.
            std::vector<tl::VideoFrame> videoFrames;
            tl::PlayerCacheInfo cacheInfo;
            double viewZoom = 0.0;
            models::MouseActionBinding frameShuttleBinding =
                models::MouseActionBinding(ftk::MouseButton::Left, ftk::KeyModifier::Shift);
            float frameShuttleScale = 1.F;

            std::shared_ptr<ftk::Label> fileNameLabel;
            std::shared_ptr<ftk::Label> cacheLabel;
            std::shared_ptr<ftk::Label> timeLabel;
            std::shared_ptr<ftk::Label> viewZoomLabel;
            std::shared_ptr<ftk::ColorSwatch> colorPickerSwatch;
            std::shared_ptr<ftk::Label> colorPickerLabel;
            std::shared_ptr<ftk::Label> infoLabel;
            std::shared_ptr<ftk::Label> renderLabel;
            std::map<models::HUDItem, std::shared_ptr<ftk::IWidget> > hudWidgets;
            tl::Compare compare = tl::Compare::None;
            tl::CompareTime compareTime = tl::CompareTime::Relative;
            std::shared_ptr<models::FilesModelItem> a;
            std::vector<std::shared_ptr<models::FilesModelItem> > b;
            std::shared_ptr<ftk::Label> compareLabel;

            bool toastActive = false;
            bool toastHint = false;
            bool hudActive = true;
            std::shared_ptr<ftk::Label> toastLabel;
            //! The pixel probe, shown while the right button is held.
            std::shared_ptr<ftk::HorizontalLayout> probeLayout;
            std::shared_ptr<ftk::ColorSwatch> probeSwatch;
            std::shared_ptr<ftk::Label> probeLabel;
            bool probing = false;
            //! The loupe that follows the pointer while probing, how far
            //! around the pixel the values are averaged (0, 1 or 2, a
            //! square of 1, 3 or 5 pixels), and where the pointer is.
            std::shared_ptr<PixelLoupe> loupe;
            int probeAverage = 0;
            ftk::V2I probeWindowPos;
            //! The pick binding of the settings, which starts the probe too.
            models::MouseActionBinding pickBinding;
            std::shared_ptr<ftk::Observable<std::optional<PixelSample> > > pixelSample;
            //! Where the probe is, to a fraction of a pixel: the pointer
            //! moves slower than the hand while probing, and the fractions
            //! add up.
            ftk::V2F probeVirtual;
            //! Where the last event left the pointer.
            ftk::V2I probeLastPos;
#if defined(FTK_SDL3)
            //! The window whose mouse is in relative mode while probing, and
            //! how its mouse positions scale to the ones the events carry.
            SDL_Window* probeSDLWindow = nullptr;
            float probeSDLScale = 1.F;
#endif // FTK_SDL3
            ProbeMotion probeMotion;
            //! The same for the frame shuttle, without the speed curve.
            ProbeMotion shuttleMotion;
            //! A context menu opened on a right click that did not drag,
            //! kept while it is open.
            std::shared_ptr<ftk::Menu> probeMenu;
            std::weak_ptr<models::ColorModel> colorModel;
            std::shared_ptr<ViewportBinding> binding;
            std::shared_ptr<ftk::Timer> toastTimer;
            std::shared_ptr<ftk::VerticalLayout> hudLayout;
            std::map<models::HUDPos, std::shared_ptr<ftk::VerticalLayout> > hudLayouts;

            std::shared_ptr<ftk::Observer<OTIO_NS::RationalTime> > currentTimeObserver;
            std::shared_ptr<ftk::Observer<std::string> > mediaReferenceKeyObserver;
            std::shared_ptr<ftk::Observer<int> > videoLayerObserver;
            std::shared_ptr<ftk::ListObserver<tl::VideoFrame> > videoObserver;
            std::shared_ptr<ftk::Observer<tl::PlayerCacheInfo> > cacheObserver;
            std::shared_ptr<ftk::Observer<double> > fpsObserver;
            std::shared_ptr<ftk::Observer<size_t> > droppedFramesObserver;
            std::shared_ptr<ftk::Observer<std::shared_ptr<models::FilesModelItem> > > aObserver;
            std::shared_ptr<ftk::ListObserver<std::shared_ptr<models::FilesModelItem> > > bObserver;
            std::shared_ptr<ftk::Observer<tl::CompareTime> > compareTimeObserver;
            std::shared_ptr<ftk::Observer<bool> > drawEnabledObserver;
            std::shared_ptr<ftk::Observer<tl::ForegroundOptions> > fgOptionsObserver;
            std::shared_ptr<ftk::Observer<double> > viewZoomObserver;
            std::shared_ptr<ftk::ListObserver<ftk::LogItem> > messagesObserver;
            std::shared_ptr<ftk::Observer<models::HUDOptions> > hudOptionsObserver;
            std::shared_ptr<ftk::Observer<tl::TimeUnits> > timeUnitsObserver;
            std::shared_ptr<ftk::Observer<models::MouseSettings> > mouseSettingsObserver;
            std::shared_ptr<ftk::Observer<std::optional<ftk::V2I> > > pickObserver;
            std::shared_ptr<ftk::Observer<std::optional<ftk::Color4F> > > colorSampleObserver;
            std::shared_ptr<ftk::Observer<std::optional<PixelSample> > > pixelSampleObserver;
            std::shared_ptr<ftk::ListObserver<models::ReviewAnnotation> > annotationsObserver;

            enum class MouseMode
            {
                None,
                Shuttle,
                Draw,
                Erase,
                Probe
            };
            struct MouseData
            {
                MouseMode mode = MouseMode::None;
                ftk::V2I probePress;
                int probeTravel = 0;
                bool probeMoved = false;
                //! Whether the right button started the probe: a click of it
                //! that does not move opens the context menu.
                bool probeRightButton = false;
                std::optional<OTIO_NS::RationalTime> shuttleStart;
                tl::Playback shuttlePlayback = tl::Playback::Stop;
                //! The frames the shuttle has moved, from the pointer's
                //! travel, and where the pointer was last.
                double shuttleFrames = 0.0;
                int64_t shuttleOffset = 0;
                ftk::V2I shuttleLast;
#if defined(FTK_SDL3)
                //! The window whose mouse is in relative mode while the
                //! shuttle drags, and its scale from SDL's coordinates to
                //! ours.
                SDL_Window* shuttleSDLWindow = nullptr;
                float shuttleSDLScale = 1.F;
#endif // FTK_SDL3
                //! Whether the shuttle draws a pointer in place of the
                //! system's, for a remote session, and that pointer,
                //! brought round inside the viewport.
                bool shuttleDrawn = false;
                ftk::V2F shuttlePointer;
                //! Where the pointer is, drawn or the system's, for the
                //! frame number under it.
                ftk::V2I shuttleCursor;
                //! For the log: how far the drag went.
                float shuttleTravel = 0.F;
                int shuttleWraps = 0;
            };
            MouseData mouse;

            //! What playback showed, reported to the log every few seconds
            //! while it plays: the frames shown against the frames the clock
            //! went through, and the longest waits.
            struct PlaybackStats
            {
                bool active = false;
                std::chrono::steady_clock::time_point start;
                std::chrono::steady_clock::time_point lastTick;
                std::chrono::steady_clock::time_point lastShownTime;
                int ticks = 0;
                double maxTickGap = 0.0;
                std::optional<OTIO_NS::RationalTime> lastClock;
                int64_t clockFrames = 0;
                int clockJumps = 0;
                std::optional<OTIO_NS::RationalTime> lastShown;
                int shown = 0;
                int64_t shownSkipped = 0;
                double maxShownGap = 0.0;
                //! The viewport's drawing, the picture with it, on the CPU.
                int draws = 0;
                double drawTotal = 0.0;
                double drawMax = 0.0;
            };
            PlaybackStats playbackStats;

            //! The present view: on, a picture smaller than the screen
            //! filled rather than at its own pixels, and the view it came
            //! from.
            bool presentView = false;
            bool presentFill = false;
            bool presentDirty = false;
            bool presentFrameViewPrev = true;
            std::pair<ftk::V2I, double> presentViewPrev;
            std::shared_ptr<ftk::Observer<std::pair<ftk::V2I, double> > > presentObserver;

            //! The stroke being drawn, in the image pixels of its source.
            models::ReviewStroke stroke;
            int strokeSource = -1;

            std::shared_ptr<models::FilesModel> filesModel;
            std::shared_ptr<models::AnnotationsModel> annotationsModel;
            std::shared_ptr<models::DrawModel> drawModel;
        };

        void Viewport::_init(
            const std::shared_ptr<ftk::Context>& context,
            const std::shared_ptr<models::FilesModel>& filesModel,
            const std::shared_ptr<models::ColorModel>& colorModel,
            const std::shared_ptr<models::ViewportModel>& viewportModel,
            const std::shared_ptr<models::TimeUnitsModel>& timeUnitsModel,
            const std::shared_ptr<models::SettingsModel>& settingsModel,
            const std::shared_ptr<models::AnnotationsModel>& annotationsModel,
            const std::shared_ptr<models::DrawModel>& drawModel,
            const std::shared_ptr<ftk::SysLogModel>& sysLogModel,
            const std::shared_ptr<IWidget>& parent)
        {
            tl::ui::Viewport::_init(context, parent);
            FTK_P();

            setClipChildren(true);

            p.filesModel = filesModel;
            p.colorModel = colorModel;
            p.annotationsModel = annotationsModel;
            p.drawModel = drawModel;
            p.viewportModel = viewportModel;
            p.timeUnitsModel = timeUnitsModel;

            p.fileNameLabel = ftk::Label::create(context);
            p.fileNameLabel->setFont(ftk::FontType::Mono);
            p.fileNameLabel->setMarginRole(ftk::SizeRole::MarginSmall);

            p.cacheLabel = ftk::Label::create(context);
            p.cacheLabel->setFont(ftk::FontType::Mono);
            p.cacheLabel->setMarginRole(ftk::SizeRole::MarginSmall);

            p.timeLabel = ftk::Label::create(context);
            p.timeLabel->setFont(ftk::FontType::Mono);
            p.timeLabel->setMarginRole(ftk::SizeRole::MarginSmall);

            p.viewZoomLabel = ftk::Label::create(context);
            p.viewZoomLabel->setFont(ftk::FontType::Mono);
            p.viewZoomLabel->setMarginRole(ftk::SizeRole::MarginSmall);

            p.colorPickerSwatch = ftk::ColorSwatch::create(context);
            p.colorPickerSwatch->setVAlign(ftk::VAlign::Center);
            p.colorPickerLabel = ftk::Label::create(context);
            p.colorPickerLabel->setFont(ftk::FontType::Mono);
            p.colorPickerLabel->setMarginRole(ftk::SizeRole::MarginSmall);
            auto colorPickerLayout = ftk::HorizontalLayout::create(context, p.hudLayout);
            colorPickerLayout->setSpacingRole(ftk::SizeRole::SpacingSmall);
            p.colorPickerSwatch->setParent(colorPickerLayout);
            p.colorPickerLabel->setParent(colorPickerLayout);

            p.infoLabel = ftk::Label::create(context);
            p.infoLabel->setFont(ftk::FontType::Mono);
            p.infoLabel->setMarginRole(ftk::SizeRole::MarginSmall);

            p.renderLabel = ftk::Label::create(context);
            p.renderLabel->setFont(ftk::FontType::Mono);
            p.renderLabel->setMarginRole(ftk::SizeRole::MarginSmall);

            p.hudWidgets[models::HUDItem::FileName] = p.fileNameLabel;
            p.hudWidgets[models::HUDItem::Cache] = p.cacheLabel;
            p.hudWidgets[models::HUDItem::Time] = p.timeLabel;
            p.hudWidgets[models::HUDItem::ViewZoom] = p.viewZoomLabel;
            p.hudWidgets[models::HUDItem::ColorPicker] = colorPickerLayout;
            p.hudWidgets[models::HUDItem::Info] = p.infoLabel;
            p.hudWidgets[models::HUDItem::Render] = p.renderLabel;

            p.hudLayout = ftk::VerticalLayout::create(context, shared_from_this());
            p.hudLayout->setMarginRole(ftk::SizeRole::MarginSmall);
            p.hudLayout->setSpacingRole(ftk::SizeRole::None);
            for (const auto i : models::getHUDPosEnums())
            {
                if (models::HUDPos::None == i)
                    continue;
                p.hudLayouts[i] = ftk::VerticalLayout::create(context);
                p.hudLayouts[i]->setMarginRole(ftk::SizeRole::MarginInside);
                p.hudLayouts[i]->setSpacingRole(ftk::SizeRole::None);
                p.hudLayouts[i]->setBackgroundRole(ftk::ColorRole::Overlay);
            }
            p.hudLayouts[models::HUDPos::TopLeft]->setVAlign(ftk::VAlign::Top);
            p.hudLayouts[models::HUDPos::TopRight]->setVAlign(ftk::VAlign::Top);
            p.hudLayouts[models::HUDPos::BottomLeft]->setVAlign(ftk::VAlign::Bottom);
            p.hudLayouts[models::HUDPos::BottomRight]->setVAlign(ftk::VAlign::Bottom);

            p.compareLabel = ftk::Label::create(context);
            p.compareLabel->setMarginRole(ftk::SizeRole::MarginSmall);
            p.compareLabel->setBackgroundRole(ftk::ColorRole::Overlay);
            p.compareLabel->setVisible(false);

            p.toastLabel = ftk::Label::create(context);
            p.toastLabel->setMarginRole(ftk::SizeRole::MarginSmall);
            p.toastLabel->setBackgroundRole(ftk::ColorRole::Overlay);
            p.toastLabel->setClipText(true);
            p.toastLabel->setVisible(false);

            auto topLayout = ftk::HorizontalLayout::create(context, p.hudLayout);
            topLayout->setSpacingRole(ftk::SizeRole::SpacingSmall);
            topLayout->setVAlign(ftk::VAlign::Top);
            p.hudLayouts[models::HUDPos::TopLeft]->setParent(topLayout);
            auto spacer = ftk::Spacer::create(
                context, ftk::Orientation::Horizontal, topLayout);
            spacer->setStretch(ftk::Stretch::Expanding);
            p.hudLayouts[models::HUDPos::TopRight]->setParent(topLayout);

            spacer = ftk::Spacer::create(
                context, ftk::Orientation::Vertical, p.hudLayout);
            spacer->setStretch(ftk::Stretch::Expanding);

            auto noBLayout = ftk::HorizontalLayout::create(context, p.hudLayout);
            noBLayout->setVAlign(ftk::VAlign::Center);
            spacer = ftk::Spacer::create(
                context, ftk::Orientation::Horizontal, noBLayout);
            spacer->setStretch(ftk::Stretch::Expanding);
            p.compareLabel->setParent(noBLayout);
            spacer = ftk::Spacer::create(
                context, ftk::Orientation::Horizontal, noBLayout);
            spacer->setStretch(ftk::Stretch::Expanding);
            spacer = ftk::Spacer::create(
                context, ftk::Orientation::Vertical, p.hudLayout);
            spacer->setStretch(ftk::Stretch::Expanding);

            auto bottomLayout = ftk::VerticalLayout::create(context, p.hudLayout);
            bottomLayout->setSpacingRole(ftk::SizeRole::SpacingSmall);
            auto toastLayout = ftk::HorizontalLayout::create(context, bottomLayout);
            spacer = ftk::Spacer::create(
                context, ftk::Orientation::Horizontal, toastLayout);
            spacer->setStretch(ftk::Stretch::Expanding);
            p.toastLabel->setParent(toastLayout);
            spacer = ftk::Spacer::create(
                context, ftk::Orientation::Horizontal, toastLayout);
            spacer->setStretch(ftk::Stretch::Expanding);
            // The pixel probe, over the bottom row, as Blender shows its
            // image sample at the bottom of the editor.
            p.probeSwatch = ftk::ColorSwatch::create(context);
            p.probeSwatch->setVAlign(ftk::VAlign::Center);
            p.probeLabel = ftk::Label::create(context);
            p.probeLabel->setFont(ftk::FontType::Mono);
            p.probeLabel->setMarginRole(ftk::SizeRole::MarginSmall);
            auto probeRow = ftk::HorizontalLayout::create(context, bottomLayout);
            p.probeLayout = ftk::HorizontalLayout::create(context, probeRow);
            p.probeLayout->setSpacingRole(ftk::SizeRole::SpacingSmall);
            p.probeLayout->setMarginRole(ftk::SizeRole::MarginInside);
            p.probeLayout->setBackgroundRole(ftk::ColorRole::Overlay);
            p.probeSwatch->setParent(p.probeLayout);
            p.probeLabel->setParent(p.probeLayout);
            spacer = ftk::Spacer::create(
                context, ftk::Orientation::Horizontal, probeRow);
            spacer->setStretch(ftk::Stretch::Expanding);
            p.probeLayout->setVisible(false);

            p.loupe = PixelLoupe::create(
                context,
                filesModel,
                colorModel,
                viewportModel,
                shared_from_this());
            p.loupe->setVisible(false);
            p.pixelSample = ftk::Observable<std::optional<PixelSample> >::create();

            auto bottomRowLayout = ftk::HorizontalLayout::create(context, bottomLayout);
            bottomRowLayout->setSpacingRole(ftk::SizeRole::SpacingSmall);
            bottomRowLayout->setVAlign(ftk::VAlign::Bottom);
            p.hudLayouts[models::HUDPos::BottomLeft]->setParent(bottomRowLayout);
            spacer = ftk::Spacer::create(
                context, ftk::Orientation::Horizontal, bottomRowLayout);
            spacer->setStretch(ftk::Stretch::Expanding);
            p.hudLayouts[models::HUDPos::BottomRight]->setParent(bottomRowLayout);

            p.toastTimer = ftk::Timer::create(context);

            p.fpsObserver = ftk::Observer<double>::create(
                observeFPS(),
                [this](double value)
                {
                    _p->fps = value;
                    _hudUpdate();
                });

            p.droppedFramesObserver = ftk::Observer<size_t>::create(
                observeDroppedFrames(),
                [this](size_t value)
                {
                    _p->droppedFrames = value;
                    _hudUpdate();
                });

            p.aObserver = ftk::Observer<std::shared_ptr<models::FilesModelItem> >::create(
                filesModel->observeA(),
                [this](const std::shared_ptr<models::FilesModelItem>& value)
                {
                    _p->a = value;
                    _compareUpdate();
                });

            p.bObserver = ftk::ListObserver<std::shared_ptr<models::FilesModelItem> >::create(
                filesModel->observeB(),
                [this](const std::vector<std::shared_ptr<models::FilesModelItem> >& value)
                {
                    _p->b = value;
                    _compareUpdate();
                });

            p.drawEnabledObserver = ftk::Observer<bool>::create(
                drawModel->observeEnabled(),
                [this](bool)
                {
                    _cursorUpdate();
                });

            p.compareTimeObserver = ftk::Observer<tl::CompareTime>::create(
                filesModel->observeCompareTime(),
                [this](tl::CompareTime value)
                {
                    _p->compareTime = value;
                    _compareUpdate();
                });

            p.fgOptionsObserver = ftk::Observer<tl::ForegroundOptions>::create(
                viewportModel->observeForegroundOptions(),
                [this](const tl::ForegroundOptions& value)
                {
                    setForegroundOptions(value);
                });

            p.binding = ViewportBinding::create(
                filesModel,
                colorModel,
                viewportModel,
                std::dynamic_pointer_cast<tl::ui::Viewport>(shared_from_this()));
            p.binding->setChangedCallback(
                [this]
                {
                    FTK_P();
                    p.compare = getCompareOptions().compare;
                    _compareUpdate();
                    // The heads up display says what is being rendered,
                    // which the aspect ratio and the color buffer change.
                    // Without this it would only catch up when something
                    // else refreshes it -- which is every frame while
                    // playing, and nothing at all while stopped.
                    _hudUpdate();
                });

            p.viewZoomObserver = ftk::Observer<double>::create(
                observeZoom(),
                [this](double value)
                {
                    _p->viewZoom = value;
                    _hudUpdate();
                });

            p.messagesObserver = ftk::ListObserver<ftk::LogItem>::create(
                sysLogModel->observeMessages(),
                [this](const std::vector<ftk::LogItem>& value)
                {
                    FTK_P();
                    if (value.empty())
                    {
                        // Cleared, so there is nothing left to be showing.
                        p.toastLabel->setText(std::string());
                        _toastUpdate();
                        return;
                    }
                    // Errors only. The list carries warnings as well, and the
                    // status bar shows both; drawing over the image is for
                    // what stopped something from working, not for what is
                    // merely worth knowing. A warning arriving is also not a
                    // reason to cut short an error that is still up.
                    if (ftk::LogType::Error != value.back().type)
                    {
                        return;
                    }
                    // The message alone: it has just appeared, and the space
                    // over the image is better spent on what went wrong. It
                    // takes the place of a hint, and is shown or not the way
                    // errors are.
                    p.toastHint = false;
                    p.toastLabel->setText(
                        ftk::elide(
                            ftk::getLabel(value.back(), ftk::LogLabel::Message),
                            toastTextLength));
                    _toastUpdate();
                    if (!p.toastLabel->getText().empty())
                    {
                        p.toastTimer->start(
                            toastTimeout,
                            [this]
                            {
                                _p->toastLabel->setText(std::string());
                                _toastUpdate();
                            });
                    }
                });

            p.hudOptionsObserver = ftk::Observer<models::HUDOptions>::create(
                viewportModel->observeHUDOptions(),
                [this](const models::HUDOptions& value)
                {
                    _p->hudOptions = value;
                    _hudUpdate();
                    _hudLayout();
                });

            p.timeUnitsObserver = ftk::Observer<tl::TimeUnits>::create(
                timeUnitsModel->observeTimeUnits(),
                [this](tl::TimeUnits value)
                {
                    _hudUpdate();
                });

            // The overlay is drawn straight from the model, so any change to it
            // has to ask for a redraw. Drawing does that from the mouse events,
            // but undo, redo and "Clear Drawing" only touch the model: without
            // this the frame keeps showing stale strokes until some unrelated
            // event happens to repaint the window.
            p.annotationsObserver = ftk::ListObserver<models::ReviewAnnotation>::create(
                annotationsModel->observeAnnotations(),
                [this](const std::vector<models::ReviewAnnotation>&)
                {
                    setDrawUpdate();
                });

            p.mouseSettingsObserver = ftk::Observer<models::MouseSettings>::create(
                settingsModel->observeMouse(),
                [this](const models::MouseSettings& value)
                {
                    FTK_P();
                    auto i = value.bindings.find(models::MouseAction::PanView);
                    setPanBinding(
                        i != value.bindings.end() ? i->second.button : ftk::MouseButton::None,
                        i != value.bindings.end() ? i->second.modifier : ftk::KeyModifier::None);
                    i = value.bindings.find(models::MouseAction::CompareWipe);
                    setWipeBinding(
                        i != value.bindings.end() ? i->second.button : ftk::MouseButton::None,
                        i != value.bindings.end() ? i->second.modifier : ftk::KeyModifier::None);
                    // The pick binding starts the probe, as the right button
                    // does, rather than the base class's picker: that reads a
                    // pixel of the screen, which zoomed out is several of the
                    // picture's blended.
                    i = value.bindings.find(models::MouseAction::Pick);
                    p.pickBinding = i != value.bindings.end() ? i->second : models::MouseActionBinding();
                    setPickBinding(ftk::MouseButton::None, ftk::KeyModifier::None);
                    i = value.bindings.find(models::MouseAction::FrameShuttle);
                    p.frameShuttleBinding = i != value.bindings.end() ? i->second : models::MouseActionBinding();
                    p.frameShuttleScale = value.frameShuttleScale;
                    // Here with the other mouse settings rather than set by the
                    // main window, which left a secondary window's viewport
                    // zooming at the default scale.
                    setMouseWheelScale(value.wheelScale);
                    auto j = value.wheelBindings.find(models::WheelAction::Zoom);
                    setWheelZoomBinding(j != value.wheelBindings.end() ?
                        j->second :
                        ftk::KeyModifier::None);
                    j = value.wheelBindings.find(models::WheelAction::Scrub);
                    setWheelScrubBinding(j != value.wheelBindings.end() ?
                        j->second :
                        ftk::KeyModifier::Control);
                });

            p.pickObserver = ftk::Observer<std::optional<ftk::V2I> >::create(
                observePick(),
                [this](const std::optional<ftk::V2I>&)
                {
                    _hudUpdate();
                });

            p.colorSampleObserver = ftk::Observer<std::optional<ftk::Color4F> >::create(
                observeColorSample(),
                [this](const std::optional<ftk::Color4F>&)
                {
                    _hudUpdate();
                    _probeUpdate();
                });

            p.pixelSampleObserver = ftk::Observer<std::optional<PixelSample> >::create(
                p.pixelSample,
                [this](const std::optional<PixelSample>&)
                {
                    _hudUpdate();
                });
        }

        Viewport::Viewport() :
            _p(new Private)
        {}

        Viewport::~Viewport()
        {}

        std::shared_ptr<Viewport> Viewport::create(
            const std::shared_ptr<ftk::Context>& context,
            const std::shared_ptr<models::FilesModel>& filesModel,
            const std::shared_ptr<models::ColorModel>& colorModel,
            const std::shared_ptr<models::ViewportModel>& viewportModel,
            const std::shared_ptr<models::TimeUnitsModel>& timeUnitsModel,
            const std::shared_ptr<models::SettingsModel>& settingsModel,
            const std::shared_ptr<models::AnnotationsModel>& annotationsModel,
            const std::shared_ptr<models::DrawModel>& drawModel,
            const std::shared_ptr<ftk::SysLogModel>& sysLogModel,
            const std::shared_ptr<IWidget>& parent)
        {
            auto out = std::shared_ptr<Viewport>(new Viewport);
            out->_init(
                context,
                filesModel,
                colorModel,
                viewportModel,
                timeUnitsModel,
                settingsModel,
                annotationsModel,
                drawModel,
                sysLogModel,
                parent);
            return out;
        }

        void Viewport::setPlayer(const std::shared_ptr<tl::Player>& player)
        {
            tl::ui::Viewport::setPlayer(player);
            FTK_P();
            if (player)
            {
                p.path = player->getPath();
                p.timeRange = player->getTimeRange();
                p.loupe->setPlayer(player);

                // The information describes the media reference being read, so
                // it is refreshed when the key changes. The observer also
                // reports the current key, which covers the new player.
                p.mediaReferenceKeyObserver = ftk::Observer<std::string>::create(
                    player->observeMediaReferenceKey(),
                    [this, player](const std::string&)
                    {
                        _p->ioInfo = player->getIOInfo();
                        // Panning and zooming move an image around the view.
                        // Media with no video has none, so the view has
                        // nothing to move and the wheel would change a zoom
                        // that shows nothing.
                        setInputEnabled(!_p->ioInfo.video.empty());
                        _hudUpdate();
                    });

                p.videoLayerObserver = ftk::Observer<int>::create(
                    player->observeVideoLayer(),
                    [this](int value)
                    {
                        _p->videoLayer = value;
                        _hudUpdate();
                    });

                p.currentTimeObserver = ftk::Observer<OTIO_NS::RationalTime>::create(
                    player->observeCurrentTime(),
                    [this](const OTIO_NS::RationalTime& value)
                    {
                        auto& stats = _p->playbackStats;
                        if (stats.active)
                        {
                            if (stats.lastClock.has_value())
                            {
                                const int64_t frames = std::llabs(std::llround(
                                    (value - stats.lastClock.value()).rescaled_to(value.rate()).value()));
                                // A loop comes round: not a step.
                                if (frames < 100)
                                {
                                    stats.clockFrames += frames;
                                    if (frames > 1)
                                    {
                                        ++stats.clockJumps;
                                    }
                                }
                            }
                            stats.lastClock = value;
                        }
                        _p->currentTime = value;
                        _hudUpdate();
                        // Annotations are per frame, so the overlay changes.
                        setDrawUpdate();
                    });

                p.videoObserver = ftk::ListObserver<tl::VideoFrame>::create(
                    player->observeCurrentVideo(),
                    [this](const std::vector<tl::VideoFrame>& value)
                    {
                        FTK_P();
                        auto& stats = p.playbackStats;
                        if (stats.active && !value.empty())
                        {
                            const OTIO_NS::RationalTime t = value.front().time;
                            if (!stats.lastShown.has_value() || t != stats.lastShown.value())
                            {
                                const auto now = std::chrono::steady_clock::now();
                                if (stats.lastShown.has_value())
                                {
                                    const int64_t frames = std::llabs(std::llround(
                                        (t - stats.lastShown.value()).rescaled_to(t.rate()).value()));
                                    if (frames > 1 && frames < 100)
                                    {
                                        stats.shownSkipped += frames - 1;
                                    }
                                    const std::chrono::duration<double> gap = now - stats.lastShownTime;
                                    stats.maxShownGap = std::max(stats.maxShownGap, gap.count());
                                }
                                ++stats.shown;
                                stats.lastShown = t;
                                stats.lastShownTime = now;
                            }
                        }
                        p.videoFramesSize = value.size();
                        const bool sizeChanged =
                            p.videoFrames.empty() != value.empty() ||
                            (!value.empty() && !p.videoFrames.empty() &&
                                !value.front().layers.empty() && !p.videoFrames.front().layers.empty() &&
                                value.front().layers.front().image && p.videoFrames.front().layers.front().image &&
                                value.front().layers.front().image->getInfo().size != p.videoFrames.front().layers.front().image->getInfo().size);
                        p.videoFrames = value;
                        if (sizeChanged && p.presentView)
                        {
                            p.presentDirty = true;
                        }
                        p.binding->setVideoFramesSize(value.size());
                        _compareUpdate();
                        p.missing = false;
                        p.heldFrom.reset();
                        // The first source, which is the one the time in the
                        // heads up display is about.
                        if (!value.empty())
                        {
                            for (const auto& layer : value.front().layers)
                            {
                                if (layer.missing)
                                {
                                    p.missing = true;
                                    if (layer.heldFrom.has_value())
                                    {
                                        p.heldFrom = layer.heldFrom;
                                    }
                                }
                            }
                        }
                        _hudUpdate();
                    });

                p.cacheObserver = ftk::Observer<tl::PlayerCacheInfo>::create(
                    player->observeCacheInfo(),
                    [this](const tl::PlayerCacheInfo& value)
                    {
                        _p->cacheInfo = value;
                        _hudUpdate();
                    });
            }
            else
            {
                p.path = ftk::Path();
                p.ioInfo = tl::IOInfo();
                p.timeRange.reset();
                p.loupe->setPlayer(nullptr);
                setInputEnabled(false);
                p.mediaReferenceKeyObserver.reset();
                p.currentTime.reset();
                p.videoFrames.clear();
                p.currentTimeObserver.reset();
                p.videoObserver.reset();
                p.cacheInfo = tl::PlayerCacheInfo();
                p.cacheObserver.reset();
                _hudUpdate();
            }
            // A file arriving or going changes which items there are.
            _hudLayout();
        }

        void Viewport::setHUDActive(bool value)
        {
            FTK_P();
            if (value == p.hudActive)
                return;
            p.hudActive = value;
            _hudLayout();
        }

        void Viewport::setToastActive(bool value)
        {
            FTK_P();
            if (value == p.toastActive)
                return;
            p.toastActive = value;
            _toastUpdate();
        }

        void Viewport::showHint(const std::string& value)
        {
            FTK_P();
            p.toastHint = true;
            p.toastLabel->setText(value);
            _toastUpdate();
            p.toastTimer->start(
                hintTimeout,
                [this]
                {
                    _p->toastHint = false;
                    _p->toastLabel->setText(std::string());
                    _toastUpdate();
                });
        }

        ftk::Size2I Viewport::getSizeHint() const
        {
            return _p->hudLayout->getSizeHint();
        }

        void Viewport::setPresentView(bool value)
        {
            FTK_P();
            if (value == p.presentView)
                return;
            p.presentView = value;
            if (value)
            {
                p.presentFrameViewPrev = hasFrameView();
                p.presentViewPrev = getViewPosAndZoom();
                // Framed by the present view itself rather than the frame
                // view, which would blow a small picture up to the screen.
                setFrameView(false);
                // A picture larger than the screen fills it, a smaller one
                // is shown at its own pixels; Home has the other.
                const double fit = _presentFit();
                p.presentFill = fit <= 0.0 || fit <= 1.0;
                // Looked at again on the next tick when the view changes:
                // put right from inside the change, the change went on to
                // write the position it had over the one put right.
                p.presentObserver = ftk::Observer<std::pair<ftk::V2I, double> >::create(
                    observeViewPosAndZoom(),
                    [this](const std::pair<ftk::V2I, double>&)
                    {
                        _p->presentDirty = true;
                    });
                _presentShow();
            }
            else
            {
                p.presentObserver.reset();
                p.presentDirty = false;
                setZoomRange(ftk::RangeD(.01, 512.0));
                setViewPosAndZoom(p.presentViewPrev.first, p.presentViewPrev.second);
                setFrameView(p.presentFrameViewPrev);
            }
        }

        bool Viewport::presentHome()
        {
            FTK_P();
            if (!p.presentView)
                return false;
            // Filling the screen as best it can, or at its own pixels, by
            // turns, centered either way.
            p.presentFill = !p.presentFill;
            _presentShow();
            return true;
        }

        double Viewport::_presentFit() const
        {
            FTK_P();
            const ftk::Size2I renderSize = tl::getRenderSize(
                getCompareOptions(),
                p.binding->getDisplayOptions().aspectRatio,
                p.videoFrames);
            const ftk::Box2I& g = getGeometry();
            if (!renderSize.isValid() || g.w() <= 0 || g.h() <= 0)
                return 0.0;
            return std::min(
                g.w() / static_cast<double>(renderSize.w),
                g.h() / static_cast<double>(renderSize.h));
        }

        double Viewport::_presentZoomMin() const
        {
            FTK_P();
            // Never smaller than the screen; a picture smaller than the
            // screen, never smaller than its own pixels unless filled.
            const double fit = _presentFit();
            return fit <= 1.0 || p.presentFill ? fit : 1.0;
        }

        void Viewport::_presentCenter(double zoom)
        {
            FTK_P();
            const ftk::Size2I renderSize = tl::getRenderSize(
                getCompareOptions(),
                p.binding->getDisplayOptions().aspectRatio,
                p.videoFrames);
            const ftk::Box2I& g = getGeometry();
            if (!renderSize.isValid() || g.w() <= 0 || g.h() <= 0)
                return;
            const ftk::V2I pos(
                static_cast<int>(std::lround(g.w() / 2.0 - renderSize.w * zoom / 2.0)),
                static_cast<int>(std::lround(g.h() / 2.0 - renderSize.h * zoom / 2.0)));
            const auto viewPosAndZoom = getViewPosAndZoom();
            if (pos != viewPosAndZoom.first || zoom != viewPosAndZoom.second)
            {
                setViewPosAndZoom(pos, zoom);
            }
        }

        void Viewport::_presentShow()
        {
            FTK_P();
            const double fit = _presentFit();
            if (fit <= 0.0)
            {
                // Nothing to size by yet: put right when there is.
                p.presentDirty = true;
                return;
            }
            const double zoomMin = _presentZoomMin();
            if (getZoomRange().min() != zoomMin)
            {
                setZoomRange(ftk::RangeD(zoomMin, 512.0));
            }
            _presentCenter(p.presentFill ? fit : 1.0);
            p.presentDirty = false;
        }

        void Viewport::_presentViewUpdate()
        {
            FTK_P();
            if (!p.presentView)
                return;
            const double fit = _presentFit();
            if (fit <= 0.0)
                return;
            const double zoomMin = _presentZoomMin();
            if (getZoomRange().min() != zoomMin)
            {
                setZoomRange(ftk::RangeD(zoomMin, 512.0));
            }
            // At the smallest it can be it is held, centered; above it, it
            // moves freely.
            if (getZoom() <= zoomMin * 1.0001)
            {
                _presentCenter(zoomMin);
            }
        }

        void Viewport::setGeometry(const ftk::Box2I& value)
        {
            tl::ui::Viewport::setGeometry(value);
            FTK_P();
            // The screen it is held to may have changed.
            p.presentDirty = p.presentView;
            p.hudLayout->setGeometry(value);
            if (p.probing)
            {
                _loupeGeometry();
            }
        }

        void Viewport::mouseMoveEvent(ftk::MouseMoveEvent& event)
        {
            tl::ui::Viewport::mouseMoveEvent(event);
            FTK_P();
            switch (p.mouse.mode)
            {
            case Private::MouseMode::Draw:
                _drawContinue(event.pos - getGeometry().min);
                break;
            case Private::MouseMode::Erase:
                _erase(event.pos - getGeometry().min);
                break;
            case Private::MouseMode::Probe:
            {
#if defined(FTK_SDL3)
                if (p.probeSDLWindow)
                {
                    // The motion gathered from the mouse's own counts.
                    const ftk::V2F d(p.probeMotion.dx, p.probeMotion.dy);
                    p.probeMotion.dx = 0.F;
                    p.probeMotion.dy = 0.F;
                    _probeMove(d);
                    break;
                }
#endif // FTK_SDL3
                // Without SDL's raw motion, the pointer's own.
                const ftk::V2I d = event.pos - p.probeLastPos;
                p.probeLastPos = event.pos;
                _probeMove(ftk::V2F(d.x, d.y));
                break;
            }
            case Private::MouseMode::Shuttle:
            {
#if defined(FTK_SDL3)
                if (p.mouse.shuttleSDLWindow)
                {
                    // The motion gathered from the mouse's own counts.
                    const ftk::V2F d(p.shuttleMotion.dx, p.shuttleMotion.dy);
                    p.shuttleMotion.dx = 0.F;
                    p.shuttleMotion.dy = 0.F;
                    _shuttleMove(d * p.mouse.shuttleSDLScale);
                    break;
                }
#endif // FTK_SDL3
                // Without SDL's relative motion, the pointer's own.
                const ftk::V2I d = event.pos - p.mouse.shuttleLast;
                p.mouse.shuttleLast = event.pos;
                _shuttleMove(ftk::V2F(d.x, d.y));
                break;
            }
            default: break;
            }
        }

        void Viewport::mousePressEvent(ftk::MouseClickEvent& event)
        {
            tl::ui::Viewport::mousePressEvent(event);
            FTK_P();
            // Shuttling is ours rather than the base class's, so it needs
            // this test of its own; without it it would go on working in a
            // view that takes no input.
            if (!isInputEnabled())
            {
                return;
            }

            // The plain right button samples the pixel under the pointer
            // while it is held, as in Blender's image editor. A right click
            // that does not move still opens the context menu, on release.
            // A right button bound to something in the settings keeps it.
            const bool rightButton =
                ftk::MouseButton::Right == event.button &&
                0 == event.modifiers;
            const bool pickBinding =
                p.pickBinding.button != ftk::MouseButton::None &&
                p.pickBinding.button == event.button &&
                ftk::checkKeyModifier(p.pickBinding.modifier, event.modifiers);
            if ((rightButton || pickBinding) && !event.accept)
            {
                event.accept = true;
                takeKeyFocus();
                p.mouse.mode = Private::MouseMode::Probe;
                p.mouse.probePress = event.pos;
                p.mouse.probeTravel = 0;
                p.mouse.probeMoved = false;
                p.mouse.probeRightButton = rightButton;
                p.probeVirtual = ftk::V2F(event.pos.x, event.pos.y);
                p.probeLastPos = event.pos;
#if defined(FTK_SDL3)
                // The mouse in relative mode while the button is held: the
                // pointer is hidden -- the loupe is the pointer -- and the
                // motion is the mouse's own, without the system's
                // acceleration, which turned slow small moves into none. It
                // is slowed to about two of the mouse's counts for a pixel
                // of the picture; zoomed in far enough that the picture's
                // pixels are big on screen, it goes at its own speed.
                if (SDL_Window* window = SDL_GetMouseFocus())
                {
                    float x = 0.F;
                    float y = 0.F;
                    SDL_GetMouseState(&x, &y);
                    p.probeSDLScale = x > 0.F ? event.pos.x / x : 1.F;
                    if (!(p.probeSDLScale > 0.F))
                    {
                        p.probeSDLScale = 1.F;
                    }
                    // The speed curve: slow moves at a tenth of a pixel of
                    // the picture a count, for the pixel wanted; fast ones
                    // up to one and a quarter of the viewport's pixels a
                    // count, to cross the picture.
                    const float zoom = static_cast<float>(std::max(getZoom(), 0.0001));
                    p.probeMotion = ProbeMotion();
                    p.probeMotion.minGain = 0.1F * zoom;
                    p.probeMotion.maxGain = std::max(p.probeMotion.minGain, 1.25F);
                    SDL_ResetHint(SDL_HINT_MOUSE_RELATIVE_SPEED_SCALE);
                    if (SDL_SetWindowRelativeMouseMode(window, true))
                    {
                        p.probeSDLWindow = window;
                        SDL_AddEventWatch(&probeWatch, &p.probeMotion);
                    }
                }
#endif // FTK_SDL3
                p.probing = true;
                p.probeLayout->setVisible(true);
                // The pixel alone to start with; the wheel widens it.
                p.probeAverage = 0;
                p.loupe->setAverage(p.probeAverage);
                p.loupe->setVisible(true);
                _probeAt(event.pos);
                _probeUpdate();
                return;
            }

            // Drawing owns the plain left button while it is enabled, which is
            // why it is an explicit mode: it displaces the frame shuttle.
            if (p.drawModel->isEnabled() &&
                ftk::MouseButton::Left == event.button &&
                0 == event.modifiers)
            {
                // Claim the button, as the shuttle does below. Without this
                // the release never reaches us: the stroke is never
                // committed, keeps growing on every move, and the undo that
                // follows has to copy all of it.
                event.accept = true;
                takeKeyFocus();
                const ftk::Box2I& g = getGeometry();
                const ftk::V2I pos = event.pos - g.min;
                if (models::DrawTool::Eraser == p.drawModel->getTool())
                {
                    p.mouse.mode = Private::MouseMode::Erase;
                    _erase(pos);
                }
                else
                {
                    p.mouse.mode = Private::MouseMode::Draw;
                    _drawBegin(pos);
                }
                return;
            }

            // Shift with a binding that has no modifier of its own is the
            // fine shuttle, from the start.
            const bool fineShuttle =
                ftk::KeyModifier::None == p.frameShuttleBinding.modifier &&
                static_cast<int>(ftk::KeyModifier::Shift) == event.modifiers &&
                !event.accept;
            if (p.frameShuttleBinding.button == event.button &&
                (ftk::checkKeyModifier(p.frameShuttleBinding.modifier, event.modifiers) || fineShuttle))
            {
                // The base class only claims the buttons bound to its own
                // actions, so claim ours here; an unbound button is left
                // free to open a context menu.
                event.accept = true;
                takeKeyFocus();
                p.mouse.mode = Private::MouseMode::Shuttle;
                p.mouse.shuttleLast = event.pos;
#if defined(FTK_SDL3)
                // The mouse in relative mode while the button is held, as
                // for the probe: its motion is counted however far it goes.
                // The motion has the system's pointer speed, so the shuttle
                // goes as far as the pointer would. The pointer comes back
                // from the other side of the viewport when it reaches one,
                // as in Blender.
                //
                // At the computer itself, that is the system's pointer,
                // moved by the system -- a drawn one lags behind it by a
                // frame or two -- held in the viewport, as Blender holds it
                // in the editor it drags in, and put on the other side at an
                // edge. Over Parsec it is hidden and the shuttle draws its
                // own: a pointer driven from outside goes back where the
                // other end puts it, and Parsec sends the mouse's own motion
                // only while the pointer is hidden.
                p.mouse.shuttlePointer = ftk::V2F(event.pos.x, event.pos.y);
                p.mouse.shuttleCursor = event.pos;
                p.mouse.shuttleDrawn = isParsecConnected();
                if (SDL_Window* window = SDL_GetMouseFocus())
                {
                    SDL_SetHint(SDL_HINT_MOUSE_RELATIVE_SYSTEM_SCALE, "1");
                    if (!p.mouse.shuttleDrawn)
                    {
                        SDL_SetHint(SDL_HINT_MOUSE_RELATIVE_CURSOR_VISIBLE, "1");
                        SDL_SetHint(SDL_HINT_MOUSE_RELATIVE_MODE_CENTER, "0");
                    }
                    // Gains of one: the counts as they come.
                    p.shuttleMotion = ProbeMotion();
                    if (SDL_SetWindowRelativeMouseMode(window, true))
                    {
                        p.mouse.shuttleSDLWindow = window;
                        if (auto ftkWindow = getWindow())
                        {
                            p.mouse.shuttleSDLScale = ftkWindow->getContentScale();
                        }
                        if (!(p.mouse.shuttleSDLScale > 0.F))
                        {
                            p.mouse.shuttleSDLScale = 1.F;
                        }
                        SDL_AddEventWatch(&probeWatch, &p.shuttleMotion);
                        if (!p.mouse.shuttleDrawn)
                        {
                            const ftk::Box2I& g = getGeometry();
                            const float scale = p.mouse.shuttleSDLScale;
                            SDL_Rect rect;
                            rect.x = static_cast<int>(std::ceil(g.min.x / scale));
                            rect.y = static_cast<int>(std::ceil(g.min.y / scale));
                            rect.w = std::max(static_cast<int>(std::floor(g.w() / scale)) - 1, 1);
                            rect.h = std::max(static_cast<int>(std::floor(g.h() / scale)) - 1, 1);
                            SDL_SetWindowMouseRect(window, &rect);
                        }
                        setDrawUpdate();
                    }
                    else
                    {
                        _log(ftk::Format("Shuttle: no relative mode: {0}").
                            arg(SDL_GetError()));
                        SDL_ResetHint(SDL_HINT_MOUSE_RELATIVE_SYSTEM_SCALE);
                        SDL_ResetHint(SDL_HINT_MOUSE_RELATIVE_CURSOR_VISIBLE);
                        SDL_ResetHint(SDL_HINT_MOUSE_RELATIVE_MODE_CENTER);
                    }
                }
#endif // FTK_SDL3
                if (auto player = getPlayer())
                {
                    p.mouse.shuttlePlayback = player->getPlayback();
                    player->stop();
                    p.mouse.shuttleStart = player->getCurrentTime();
                }
            }
        }

        void Viewport::mouseReleaseEvent(ftk::MouseClickEvent& event)
        {
            tl::ui::Viewport::mouseReleaseEvent(event);
            FTK_P();
            if (Private::MouseMode::Probe == p.mouse.mode)
            {
                p.mouse.mode = Private::MouseMode::None;
                p.probing = false;
                p.probeLayout->setVisible(false);
                p.loupe->setVisible(false);
#if defined(FTK_SDL3)
                if (p.probeSDLWindow)
                {
                    SDL_RemoveEventWatch(&probeWatch, &p.probeMotion);
                    SDL_SetWindowRelativeMouseMode(p.probeSDLWindow, false);
                    // The pointer comes back where the probe was, not where
                    // the hand would have taken it.
                    SDL_WarpMouseInWindow(
                        p.probeSDLWindow,
                        p.probeWindowPos.x / p.probeSDLScale,
                        p.probeWindowPos.y / p.probeSDLScale);
                    p.probeSDLWindow = nullptr;
                }
#endif // FTK_SDL3
                if (p.mouse.probeRightButton && !p.mouse.probeMoved && !event.cancel)
                {
                    const auto& callback = getContextMenuCallback();
                    if (callback)
                    {
                        auto menu = callback();
                        auto window = getWindow();
                        if (menu && !menu->isEmpty() && window)
                        {
                            p.probeMenu = menu;
                            menu->open(window, event.pos);
                        }
                    }
                }
                return;
            }
            if (Private::MouseMode::Shuttle == p.mouse.mode)
            {
                _shuttleEnd();
            }
            if (Private::MouseMode::Draw == p.mouse.mode)
            {
                // Commit the stroke as one undoable step, unless the press
                // was cancelled: the first finger of a pinch is not a
                // stroke.
                if (event.cancel)
                {
                    p.stroke = models::ReviewStroke();
                    p.strokeSource = -1;
                    setDrawUpdate();
                }
                else
                {
                    _drawEnd();
                }
            }
            else if (Private::MouseMode::Shuttle == p.mouse.mode && event.cancel)
            {
                // Back to the frame, and playing if it was: the first
                // finger of a gesture was not shuttling.
                if (auto player = getPlayer())
                {
                    if (p.mouse.shuttleStart.has_value())
                    {
                        player->seek(p.mouse.shuttleStart.value());
                    }
                    player->setPlayback(p.mouse.shuttlePlayback);
                }
            }
            p.mouse = Private::MouseData();
        }

        void Viewport::mouseEnterEvent(ftk::MouseEnterEvent& event)
        {
            tl::ui::Viewport::mouseEnterEvent(event);
            _cursorUpdate();
        }

        void Viewport::mouseLeaveEvent()
        {
            tl::ui::Viewport::mouseLeaveEvent();
            _cursorUpdate();
        }

        void Viewport::_cursorUpdate()
        {
            FTK_P();
            if (auto window = getWindow())
            {
                window->setCursor(
                    _isMouseInside() && p.drawModel->isEnabled() ?
                    ftk::CursorShape::Crosshair :
                    ftk::CursorShape::Arrow);
            }
        }

        std::vector<ftk::Box2I> Viewport::_sourceBoxes() const
        {
            FTK_P();
            // Mirror what tl::ui::Viewport uses when it draws, so the hit test
            // and the overlay land exactly where the image does.
            return tl::getBoxes(
                getCompareOptions(),
                p.binding->getDisplayOptions().aspectRatio,
                p.videoFrames);
        }

        bool Viewport::_sourceShown(int index) const
        {
            FTK_P();
            // The comparisons that put the files over each other give them all
            // the same box, so drawings of B landed on A's picture, and a new
            // stroke could only ever reach A, the first box hit. Only A is
            // drawn on there; B's drawings return side by side or on their own.
            switch (p.compare)
            {
            case tl::Compare::Wipe:
            case tl::Compare::Overlay:
            case tl::Compare::Difference:
            case tl::Compare::Butterfly:
                return 0 == index;
            default: break;
            }
            return tl::isShown(p.compare, static_cast<size_t>(index));
        }

        Viewport::SourceHit Viewport::_hitTest(const ftk::V2I& widgetPos) const
        {
            FTK_P();
            SourceHit out;
            // Widget -> render space: the inverse of translate(viewPos) * scale.
            const double zoom = getZoom();
            if (zoom <= 0.0)
            {
                return out;
            }
            const ftk::V2I& viewPos = getViewPos();
            const ftk::V2F render(
                static_cast<float>((widgetPos.x - viewPos.x) / zoom),
                static_cast<float>((widgetPos.y - viewPos.y) / zoom));

            const auto boxes = _sourceBoxes();
            for (size_t i = 0; i < boxes.size() && i < p.videoFrames.size(); ++i)
            {
                if (!_sourceShown(static_cast<int>(i)))
                {
                    continue;
                }
                const ftk::Box2I& box = boxes[i];
                if (render.x < box.min.x || render.x > box.max.x ||
                    render.y < box.min.y || render.y > box.max.y ||
                    box.w() <= 0 || box.h() <= 0)
                {
                    continue;
                }
                const auto& video = p.videoFrames[i];
                if (video.layers.empty() || !video.layers[0].image)
                {
                    continue;
                }
                const ftk::Size2I imageSize = video.layers[0].image->getSize();
                if (imageSize.w <= 0 || imageSize.h <= 0)
                {
                    continue;
                }
                // Render space -> the source's own pixels.
                out.index = static_cast<int>(i);
                out.pos = ftk::V2F(
                    (render.x - box.min.x) * imageSize.w / static_cast<float>(box.w()),
                    (render.y - box.min.y) * imageSize.h / static_cast<float>(box.h()));
                // The mirror flips the image inside its box, so the same
                // flip takes the hit back to the image's own pixels. Strokes
                // are stored unmirrored, and follow the image when the
                // mirror changes.
                if (p.binding->getDisplayOptions().mirror.x)
                {
                    out.pos.x = imageSize.w - out.pos.x;
                }
                if (p.binding->getDisplayOptions().mirror.y)
                {
                    out.pos.y = imageSize.h - out.pos.y;
                }
                out.scale = box.w() / static_cast<float>(imageSize.w);
                break;
            }
            return out;
        }

        ftk::V2F Viewport::_imageToWidget(int index, const ftk::V2F& imagePos) const
        {
            FTK_P();
            const auto boxes = _sourceBoxes();
            if (index < 0 ||
                index >= static_cast<int>(boxes.size()) ||
                index >= static_cast<int>(p.videoFrames.size()))
            {
                return ftk::V2F();
            }
            const ftk::Box2I& box = boxes[index];
            const auto& video = p.videoFrames[index];
            if (video.layers.empty() || !video.layers[0].image)
            {
                return ftk::V2F();
            }
            const ftk::Size2I imageSize = video.layers[0].image->getSize();
            if (imageSize.w <= 0 || imageSize.h <= 0)
            {
                return ftk::V2F();
            }
            // The inverse of the flip in _hitTest: stored image pixels back
            // to where the mirror shows them.
            ftk::V2F pos = imagePos;
            if (p.binding->getDisplayOptions().mirror.x)
            {
                pos.x = imageSize.w - pos.x;
            }
            if (p.binding->getDisplayOptions().mirror.y)
            {
                pos.y = imageSize.h - pos.y;
            }
            const float renderX = box.min.x + pos.x * box.w() / static_cast<float>(imageSize.w);
            const float renderY = box.min.y + pos.y * box.h() / static_cast<float>(imageSize.h);
            const double zoom = getZoom();
            const ftk::V2I& viewPos = getViewPos();
            return ftk::V2F(
                static_cast<float>(viewPos.x + renderX * zoom),
                static_cast<float>(viewPos.y + renderY * zoom));
        }

        void Viewport::_drawBegin(const ftk::V2I& widgetPos)
        {
            FTK_P();
            const SourceHit hit = _hitTest(widgetPos);
            if (hit.index < 0)
            {
                return;
            }
            {
                const auto& drawModel = p.drawModel;
                p.strokeSource = hit.index;
                p.stroke = models::ReviewStroke();
                p.stroke.color = drawModel->getColor();
                p.stroke.width = drawModel->getSize();
                p.stroke.points.push_back(hit.pos);
                setDrawUpdate();
            }
        }

        void Viewport::_drawContinue(const ftk::V2I& widgetPos)
        {
            FTK_P();
            if (p.strokeSource < 0)
            {
                return;
            }
            const SourceHit hit = _hitTest(widgetPos);
            // Keep the stroke on the source it started on, so dragging across a
            // comparison boundary does not tear it in two.
            if (hit.index != p.strokeSource)
            {
                return;
            }
            if (!p.stroke.points.empty())
            {
                const ftk::V2F& last = p.stroke.points.back();
                const float dx = hit.pos.x - last.x;
                const float dy = hit.pos.y - last.y;
                // Drop points the mouse barely moved, to keep strokes compact.
                if (dx * dx + dy * dy < .25F)
                {
                    return;
                }
            }
            p.stroke.points.push_back(hit.pos);
            setDrawUpdate();
        }

        void Viewport::_drawEnd()
        {
            FTK_P();
            if (p.strokeSource >= 0 && !p.stroke.points.empty())
            {
                {
                    const auto& active = p.filesModel->getActive();
                    if (p.strokeSource < static_cast<int>(active.size()))
                    {
                        p.annotationsModel->addStroke(
                            active[p.strokeSource]->id,
                            *p.currentTime,
                            p.stroke);
                    }
                }
            }
            p.stroke = models::ReviewStroke();
            p.strokeSource = -1;
            setDrawUpdate();
        }

        void Viewport::_erase(const ftk::V2I& widgetPos)
        {
            FTK_P();
            const SourceHit hit = _hitTest(widgetPos);
            if (hit.index < 0)
            {
                return;
            }
            {
                const auto& active = p.filesModel->getActive();
                if (hit.index < static_cast<int>(active.size()))
                {
                    // The eraser removes whole strokes it touches; its reach
                    // follows the tool size.
                    p.annotationsModel->eraseStrokes(
                        active[hit.index]->id,
                        *p.currentTime,
                        hit.pos,
                        p.drawModel->getSize());
                }
            }
        }

        void Viewport::drawEvent(const ftk::Box2I& drawRect, const ftk::DrawEvent& event)
        {
            const auto drawStart = std::chrono::steady_clock::now();
            tl::ui::Viewport::drawEvent(drawRect, event);
            FTK_P();
            // Timed for the playback report, however the drawing ends.
            struct DrawTimer
            {
                Private::PlaybackStats& stats;
                std::chrono::steady_clock::time_point start;
                ~DrawTimer()
                {
                    if (stats.active)
                    {
                        const std::chrono::duration<double> d =
                            std::chrono::steady_clock::now() - start;
                        ++stats.draws;
                        stats.drawTotal += d.count();
                        stats.drawMax = std::max(stats.drawMax, d.count());
                    }
                }
            } drawTimer{ p.playbackStats, drawStart };

            const ftk::Box2I& g = getGeometry();
            const double zoom = getZoom();
            const auto& active = p.filesModel->getActive();

            // Keep the overlay inside the viewport: a stroke zoomed past the
            // edges would otherwise be painted over the panels and toolbars.
            const bool clipRectEnabledPrev = event.render->getClipRectEnabled();
            const ftk::Box2I clipRectPrev = event.render->getClipRect();
            event.render->setClipRectEnabled(true);
            event.render->setClipRect(ftk::intersect(g, clipRectPrev));

            // Draw a stroke, converting the stored image pixels back to the
            // screen so it tracks the zoom, the pan and the comparison mode.
            auto drawStroke = [this, &event, &g, zoom](
                int index,
                const models::ReviewStroke& stroke,
                float scale)
            {
                if (stroke.points.empty())
                {
                    return;
                }
                // To the screen first, then smooth and thicken there, so the
                // curve stays smooth at any zoom.
                std::vector<ftk::V2F> path;
                path.reserve(stroke.points.size());
                for (const auto& point : stroke.points)
                {
                    ftk::V2F p = _imageToWidget(index, point);
                    p.x += g.min.x;
                    p.y += g.min.y;
                    path.push_back(p);
                }
                const float width = std::max(1.F, stroke.width * scale * static_cast<float>(zoom));
                // Thin the captured points before smoothing, then subdivide:
                // clustered controls are what make the spline overshoot.
                event.render->drawMesh(
                    models::strokeMesh(models::smoothPath(models::simplifyPath(path, 3.F), 12), width),
                    stroke.color);
            };

            const auto boxes = _sourceBoxes();
            auto sourceScale = [this, &boxes](int index) -> float
            {
                FTK_P();
                if (index < 0 ||
                    index >= static_cast<int>(boxes.size()) ||
                    index >= static_cast<int>(p.videoFrames.size()))
                {
                    return 1.F;
                }
                const auto& video = p.videoFrames[index];
                if (video.layers.empty() || !video.layers[0].image)
                {
                    return 1.F;
                }
                const int w = video.layers[0].image->getSize().w;
                return w > 0 ? boxes[index].w() / static_cast<float>(w) : 1.F;
            };

            // The strokes already committed on this frame.
            const auto& annotations = p.annotationsModel->getAnnotations();
            for (const auto& annotation : annotations)
            {
                if (!models::sameTime(annotation.time, p.currentTime))
                {
                    continue;
                }
                int index = -1;
                for (size_t i = 0; i < active.size(); ++i)
                {
                    if (active[i]->id == annotation.sourceId)
                    {
                        index = static_cast<int>(i);
                        break;
                    }
                }
                if (index < 0 || !_sourceShown(index))
                {
                    continue;
                }
                const float scale = sourceScale(index);
                for (const auto& stroke : annotation.strokes)
                {
                    drawStroke(index, stroke, scale);
                }
            }

            // The stroke currently under the cursor.
            if (p.strokeSource >= 0)
            {
                drawStroke(p.strokeSource, p.stroke, sourceScale(p.strokeSource));
            }

            event.render->setClipRectEnabled(clipRectEnabledPrev);
            event.render->setClipRect(clipRectPrev);
        }

        void Viewport::drawOverlayEvent(const ftk::Box2I& drawRect, const ftk::DrawEvent& event)
        {
            tl::ui::Viewport::drawOverlayEvent(drawRect, event);
            FTK_P();
#if defined(FTK_SDL3)
            // The shuttle's pointer, in place of the system's: a double
            // arrow, as Blender's while it scrubs, white edged with black.
            if (Private::MouseMode::Shuttle == p.mouse.mode &&
                p.mouse.shuttleSDLWindow &&
                p.mouse.shuttleDrawn)
            {
                const float s = event.displayScale;
                const ftk::V2F c(
                    std::floor(p.mouse.shuttlePointer.x) + .5F,
                    std::floor(p.mouse.shuttlePointer.y) + .5F);
                const auto arrow = [&c, s](float grow)
                {
                    // A bar between two heads, each triangle both ways round:
                    // the renderer culls one.
                    const float head = 11.F * s + grow * 1.6F;
                    const float neck = 5.F * s - grow * .4F;
                    const float half = 5.F * s + grow;
                    const float bar = 1.F * s + grow;
                    ftk::TriMesh2F mesh;
                    mesh.v = {
                        ftk::V2F(c.x - head, c.y),
                        ftk::V2F(c.x - neck, c.y - half),
                        ftk::V2F(c.x - neck, c.y + half),
                        ftk::V2F(c.x + head, c.y),
                        ftk::V2F(c.x + neck, c.y - half),
                        ftk::V2F(c.x + neck, c.y + half),
                        ftk::V2F(c.x - neck, c.y - bar),
                        ftk::V2F(c.x + neck, c.y - bar),
                        ftk::V2F(c.x + neck, c.y + bar),
                        ftk::V2F(c.x - neck, c.y + bar)
                    };
                    const std::vector<std::array<size_t, 3> > triangles = {
                        { 1, 2, 3 }, { 4, 5, 6 }, { 7, 8, 9 }, { 7, 9, 10 }
                    };
                    for (const auto& t : triangles)
                    {
                        ftk::Triangle2 a;
                        a.v[0].v = t[0];
                        a.v[1].v = t[1];
                        a.v[2].v = t[2];
                        mesh.triangles.push_back(a);
                        ftk::Triangle2 b;
                        b.v[0].v = t[0];
                        b.v[1].v = t[2];
                        b.v[2].v = t[1];
                        mesh.triangles.push_back(b);
                    }
                    return mesh;
                };
                const bool clipRectEnabledPrev = event.render->getClipRectEnabled();
                const ftk::Box2I clipRectPrev = event.render->getClipRect();
                event.render->setClipRectEnabled(true);
                event.render->setClipRect(ftk::intersect(getGeometry(), clipRectPrev));
                event.render->drawMesh(arrow(1.5F * s), ftk::Color4F(0.F, 0.F, 0.F, 1.F));
                event.render->drawMesh(arrow(0.F), ftk::Color4F(1.F, 1.F, 1.F, 1.F));
                event.render->setClipRectEnabled(clipRectEnabledPrev);
                event.render->setClipRect(clipRectPrev);
            }

            // The frame under the pointer while the shuttle drags: the
            // number that names the file and the position from the first
            // frame, as the heads up display has them.
            if (Private::MouseMode::Shuttle == p.mouse.mode &&
                p.mouse.shuttleSDLWindow &&
                p.currentTime.has_value())
            {
                const OTIO_NS::RationalTime& t = p.currentTime.value();
                std::string text = tl::timeToText(t, tl::TimeUnits::Frames);
                if (p.timeRange.has_value())
                {
                    const OTIO_NS::RationalTime start =
                        p.timeRange->start_time().rescaled_to(t.rate());
                    text += ftk::Format("  #{0}").
                        arg(static_cast<int64_t>(std::llround(t.value() - start.value())));
                }
                const float s = event.displayScale;
                const ftk::FontInfo fontInfo = event.style->getFont(ftk::FontType::Regular, s);
                const ftk::FontMetrics fontMetrics = event.fontSystem->getMetrics(fontInfo);
                const ftk::Size2I textSize = event.fontSystem->getSize(text, fontInfo);
                const int margin = event.style->getSizeRole(ftk::SizeRole::MarginInside, s);
                // Below the pointer: the drawn arrow is centered on it, the
                // system's hangs from it.
                const ftk::V2I& c = p.mouse.shuttleCursor;
                const int below = static_cast<int>((p.mouse.shuttleDrawn ? 12.F : 26.F) * s);
                const ftk::Box2I& g = getGeometry();
                ftk::Box2I box(
                    c.x - textSize.w / 2 - margin,
                    c.y + below,
                    textSize.w + margin * 2,
                    fontMetrics.lineHeight + margin * 2);
                // Kept in the viewport, above the pointer when there is no
                // room under it.
                if (box.max.y > g.max.y)
                {
                    box = ftk::Box2I(box.min.x, c.y - below - box.h(), box.w(), box.h());
                }
                box = ftk::Box2I(
                    std::clamp(box.min.x, g.min.x, std::max(g.min.x, g.max.x - box.w() + 1)),
                    std::clamp(box.min.y, g.min.y, std::max(g.min.y, g.max.y - box.h() + 1)),
                    box.w(),
                    box.h());
                event.render->drawRect(box, event.style->getColorRole(ftk::ColorRole::Overlay));
                event.render->drawText(
                    event.fontSystem->getGlyphs(text, fontInfo),
                    fontMetrics,
                    ftk::V2I(box.min.x + margin, box.min.y + margin),
                    event.style->getColorRole(ftk::ColorRole::Text));
            }
#endif // FTK_SDL3
        }

        void Viewport::_compareUpdate()
        {
            FTK_P();
            // Everything but A needs a B file that is not the A file. Neither
            // draws anything, and an empty picture is the one thing a black
            // frame, an unreadable file and a comparison out of sync all look
            // like as well.
            std::string s;
            if (p.compare != tl::Compare::None)
            {
                if (p.videoFramesSize < 2)
                {
                    if (p.b.empty())
                    {
                        s = "No B file selected";
                    }
                    else if (p.a && p.a->videoLayers.empty())
                    {
                        // The player is built from "A", and one with no
                        // video runs no video at all -- the comparison has
                        // nothing to composite, however real the B file is.
                        s = "A has no video to compare";
                    }
                    // Otherwise the frames are still loading, which is not
                    // worth an announcement.
                }
                else if (p.a && !p.b.empty())
                {
                    // Only when there is nothing else in B: a file compared
                    // with itself alongside others still has something to
                    // show.
                    const bool allA = std::all_of(
                        p.b.begin(),
                        p.b.end(),
                        [this](const std::shared_ptr<models::FilesModelItem>& i)
                        {
                            return i == _p->a;
                        });
                    if (allA)
                    {
                        s = "A and B are the same file";
                    }
                    else if (tl::CompareTime::Absolute == p.compareTime &&
                        p.a->timeRange.has_value())
                    {
                        // Timecode sync with no timecode in common: every B
                        // frame maps outside its file, so B never draws.
                        // Decided from the ranges rather than the missing
                        // frame, which is also what a frame still loading
                        // looks like.
                        const bool noOverlap = std::all_of(
                            p.b.begin(),
                            p.b.end(),
                            [this](const std::shared_ptr<models::FilesModelItem>& i)
                            {
                                return i->timeRange.has_value() &&
                                    !i->timeRange->intersects(
                                        *_p->a->timeRange);
                            });
                        if (noOverlap)
                        {
                            s = "A and B timecodes do not overlap";
                        }
                    }
                }
            }
            p.compareLabel->setText(s);
            p.compareLabel->setVisible(!s.empty());
            ftk::setScreenshotTag(p.compareLabel, !s.empty() ? "View.Compare" : "");
        }

        void Viewport::_hudUpdate()
        {
            FTK_P();

            // For a sequence, the image on screen rather than the
            // pattern: which frame a picture came from is the question a
            // sequence of renders gets asked. Sequence time is the frame
            // number, so the value is the name (#490).
            std::string s =
                p.path.isSeq() && p.currentTime.has_value() ?
                p.path.getFrame(static_cast<int64_t>(p.currentTime->value())) :
                p.path.getFileName();
            p.fileNameLabel->setText(!s.empty() ? s : "(No file)");
            ftk::setScreenshotTag(p.fileNameLabel, "View.HUD.FileName");

            std::vector<std::string> info;
            if (!p.ioInfo.video.empty())
            {
                info.push_back(std::string(ftk::Format("V: {0}").
                    arg(ftk::getLabel(
                        tl::getVideoInfo(p.ioInfo, p.videoLayer)))));
            }
            if (p.ioInfo.audio.isValid())
            {
                info.push_back(std::string(ftk::Format("A: {0}").
                    arg(tl::getLabel(p.ioInfo.audio, true))));
            }
            p.infoLabel->setText(ftk::join(info, ", "));
            p.infoLabel->setVisible(!info.empty());
            ftk::setScreenshotTag(p.infoLabel, "View.HUD.Info");

            // What is actually rendered, which the pixel aspect ratio and the
            // aspect ratio override can both move away from the media size
            // reported above. The effective pixel aspect ratio is taken back
            // out of the render size rather than read from either source, so
            // it holds whether it came from the media or from an override.
            s = std::string();
            if (!p.ioInfo.video.empty())
            {
                const ftk::ImageInfo& videoInfo = p.ioInfo.video[0];
                const ftk::Size2I renderSize = tl::getRenderSize(
                    videoInfo,
                    p.binding->getDisplayOptions().aspectRatio);
                if (renderSize.isValid() && videoInfo.size.w > 0)
                {
                    const float pixelAspectRatio =
                        renderSize.w / static_cast<float>(videoInfo.size.w);
                    s = ftk::Format("Render: {0}x{1}:{2}").
                        arg(renderSize.w).
                        arg(renderSize.h).
                        arg(ftk::aspectRatio(renderSize), 2);
                    // Square pixels are the common case and add nothing.
                    if (std::fabs(pixelAspectRatio - 1.F) > 0.001F)
                    {
                        s += ftk::Format(", PAR: {0}").
                            arg(pixelAspectRatio, 2);
                    }
                }
            }
            p.renderLabel->setText(s);
            p.renderLabel->setVisible(!s.empty());
            ftk::setScreenshotTag(p.renderLabel, "View.HUD.Render");

            // Every way of saying the time at once rather than the one the
            // time units setting picks: the frame number that names the
            // file, the position counted from the first frame of the media
            // (renders seldom start at zero), and the timecode and the
            // seconds from that first frame too -- frame 986 of a render is
            // not half a minute into anything.
            s = std::string();
            if (p.currentTime.has_value())
            {
                const OTIO_NS::RationalTime& t = p.currentTime.value();
                OTIO_NS::RationalTime fromStart = t;
                std::vector<std::string> parts;
                parts.push_back(tl::timeToText(t, tl::TimeUnits::Frames));
                if (p.timeRange.has_value())
                {
                    const OTIO_NS::RationalTime start =
                        p.timeRange->start_time().rescaled_to(t.rate());
                    fromStart = t - start;
                    const int64_t index = static_cast<int64_t>(
                        std::llround(t.value() - start.value()));
                    const int64_t duration = static_cast<int64_t>(
                        std::llround(p.timeRange->duration().rescaled_to(t.rate()).value()));
                    parts.push_back(ftk::Format("#{0}/{1}").
                        arg(index).
                        arg(std::max(duration - 1, static_cast<int64_t>(0))));
                }
                parts.push_back(tl::timeToText(fromStart, tl::TimeUnits::Timecode));
                parts.push_back(tl::timeToText(fromStart, tl::TimeUnits::Seconds) + "s");
                s = ftk::join(parts, "  ");
            }
            else if (auto timeUnitsModel = p.timeUnitsModel.lock())
            {
                s = timeUnitsModel->getLabel(p.currentTime);
            }
            std::string missing;
            if (p.missing)
            {
                // Said rather than only drawn, so which frame is standing in
                // is known and not merely that one is.
                missing = p.heldFrom.has_value() ?
                    ftk::Format(", held from {0}").
                        arg(p.heldFrom.value()).str() :
                    ", missing";
            }
            // Frames per second and frames dropped are about video, and a
            // file without any has neither -- reporting none and an
            // ever-growing count of what was never going to arrive reads as
            // a fault rather than as an absence.
            std::string timeText = ftk::Format("Time: {0}{1}").
                arg(s).
                arg(missing);
            if (!p.ioInfo.video.empty())
            {
                timeText = ftk::Format("Time: {0}, {1} FPS, {2} dropped{3}").
                    arg(s).
                    arg(p.fps, 2, 6).
                    arg(static_cast<int>(p.droppedFrames), 3).
                    arg(missing);
            }
            p.timeLabel->setText(timeText);
            ftk::setScreenshotTag(p.timeLabel, "View.HUD.Time");

            p.viewZoomLabel->setText(ftk::Format("Zoom: {0}").
                arg(p.viewZoom, 2, 6));
            ftk::setScreenshotTag(p.viewZoomLabel, "View.HUD.ViewZoom");

            // The probe's last sample: the picture's pixel as displayed.
            const auto& pixelSample = p.pixelSample->get();
            std::optional<ftk::Color4F> colorSample;
            std::optional<ftk::V2I> pick;
            if (pixelSample.has_value())
            {
                colorSample = pixelSample->display;
                pick = pixelSample->pixel;
            }
            {
                // Opaque, as the probe's swatch.
                ftk::Color4F swatch = colorSample.has_value() ? colorSample.value() : ftk::Color4F();
                swatch.a = 1.F;
                p.colorPickerSwatch->setColor(swatch);
            }
            // The HUD sits under the pointer, so a line that changes width
            // as values come and go moves exactly where the eye is. The
            // widths hold a sign and two digits, so the ordinary zero to one
            // range and a little either side of it do not move the line;
            // beyond that the field grows, as it did before. Without a
            // sample -- every time the pointer leaves the image -- the line
            // is a dash for each part and then spaces out to the width of a
            // sample: the HUD font is monospaced, so the same number of
            // characters is the same width. A dash in every field kept the
            // width too, but read as four missing values rather than none.
            const int colorWidth = 5;
            const int pickWidth = 4;
            const std::string colorPickerFormat =
                "Color: {0} {1} {2} {3}, Pixel: {4}, {5}";
            const size_t colorPickerWidth =
                ftk::Format(colorPickerFormat).
                arg(0.F, 2, colorWidth).
                arg(0.F, 2, colorWidth).
                arg(0.F, 2, colorWidth).
                arg(0.F, 2, colorWidth).
                arg(0, pickWidth).
                arg(0, pickWidth).str().size();
            std::string colorPickerText = "Color: -, Pixel: -";
            colorPickerText.resize(
                std::max(colorPickerText.size(), colorPickerWidth),
                ' ');
            if (colorSample.has_value() && pick.has_value())
            {
                colorPickerText =
                    ftk::Format(colorPickerFormat).
                    arg(colorSample.value().r, 2, colorWidth).
                    arg(colorSample.value().g, 2, colorWidth).
                    arg(colorSample.value().b, 2, colorWidth).
                    arg(colorSample.value().a, 2, colorWidth).
                    arg(pick.value().x, pickWidth).
                    arg(pick.value().y, pickWidth);
            }
            p.colorPickerLabel->setText(colorPickerText);
            ftk::setScreenshotTag(p.colorPickerLabel, "View.HUD.ColorPicker");
            ftk::setScreenshotTag(p.colorPickerSwatch, "View.HUD.ColorPickerSwatch");

            std::vector<std::string> cache;
            if (!p.ioInfo.video.empty())
            {
                cache.push_back(std::string(ftk::Format("{0}% V").
                    arg(static_cast<int>(p.cacheInfo.videoPercentage), 3)));
            }
            if (p.ioInfo.audio.isValid())
            {
                cache.push_back(std::string(ftk::Format("{0}% A").
                    arg(static_cast<int>(p.cacheInfo.audioPercentage), 3)));
            }
            s = !cache.empty() ?
                std::string(ftk::Format("Cache: {0}").arg(ftk::join(cache, ", "))) :
                std::string();
            p.cacheLabel->setText(s);
            p.cacheLabel->setVisible(!s.empty());
            ftk::setScreenshotTag(p.cacheLabel, "View.HUD.Cache");
        }

        void Viewport::_toastUpdate()
        {
            FTK_P();
            const bool visible =
                (p.toastActive || p.toastHint) && !p.toastLabel->getText().empty();
            p.toastLabel->setVisible(visible);
            // Tagged only while it is up, the same as the compare label, so
            // that a capture says whether anything was drawn over the image
            // rather than only what it would have said.
            ftk::setScreenshotTag(p.toastLabel, visible ? "View.Toast" : "");
        }

        void Viewport::_probeUpdate()
        {
            FTK_P();
            if (!p.probing)
            {
                return;
            }
            const auto& pick = observePick()->get();
            // What is displayed, from the loupe, which draws the picture at
            // the size of the file: read from the viewport, a pixel on
            // screen is several of the file's blended when zoomed out.
            std::optional<ftk::Color4F> display = p.loupe->getDisplayColor();
            if (!display.has_value())
            {
                display = observeColorSample()->get();
            }
            // The file's values, averaged over the same pixels.
            std::optional<ftk::Color4F> file;
            if (pick.has_value() && !p.videoFrames.empty())
            {
                for (const auto& layer : p.videoFrames.front().layers)
                {
                    if (layer.image)
                    {
                        ftk::Color4F sum(0.F, 0.F, 0.F, 0.F);
                        int count = 0;
                        for (int y = -p.probeAverage; y <= p.probeAverage; ++y)
                        {
                            for (int x = -p.probeAverage; x <= p.probeAverage; ++x)
                            {
                                if (const auto c = readPixel(
                                    layer.image,
                                    ftk::V2I(pick->x + x, pick->y + y)))
                                {
                                    sum.r += c->r;
                                    sum.g += c->g;
                                    sum.b += c->b;
                                    sum.a += c->a;
                                    ++count;
                                }
                            }
                        }
                        if (count > 0)
                        {
                            file = ftk::Color4F(
                                sum.r / count,
                                sum.g / count,
                                sum.b / count,
                                sum.a / count);
                        }
                        break;
                    }
                }
            }
            std::string colorSpace;
            if (auto colorModel = p.colorModel.lock())
            {
                colorSpace = colorModel->observeResolvedInput()->get();
            }
            // Opaque: the swatch is what is on screen. A pixel can add light
            // and cover nothing -- the glow of a premultiplied render has
            // color and no alpha -- and drawn with its alpha it was black.
            ftk::Color4F swatch = display.has_value() ? display.value() : ftk::Color4F();
            swatch.a = 1.F;
            p.probeSwatch->setColor(swatch);
            p.loupe->setAverageColor(display);
            if (pick.has_value())
            {
                PixelSample sample;
                sample.pixel = pick.value();
                sample.display = display;
                sample.file = file;
                sample.average = p.probeAverage;
                p.pixelSample->setIfChanged(sample);
            }
            p.probeLabel->setText(pick.has_value() ?
                getPixelProbeText(pick.value(), file, display, getLumaWeights(colorSpace)) :
                std::string("Outside the image"));
        }

        std::shared_ptr<ftk::IObservable<std::optional<PixelSample> > > Viewport::observePixelSample() const
        {
            return _p->pixelSample;
        }

        void Viewport::scrollEvent(ftk::ScrollEvent& event)
        {
            FTK_P();
            if (p.probing)
            {
                // While probing the wheel is the size of the average rather
                // than the zoom: the pixel alone, three by three, five by
                // five.
                event.accept = true;
                if (event.value.y > 0.F)
                {
                    p.probeAverage = std::min(p.probeAverage + 1, 2);
                }
                else if (event.value.y < 0.F)
                {
                    p.probeAverage = std::max(p.probeAverage - 1, 0);
                }
                p.loupe->setAverage(p.probeAverage);
                _probeUpdate();
                return;
            }
            tl::ui::Viewport::scrollEvent(event);
        }

        void Viewport::tickEvent(
            bool parentsVisible,
            bool parentsEnabled,
            const ftk::TickEvent& event)
        {
            tl::ui::Viewport::tickEvent(parentsVisible, parentsEnabled, event);
            FTK_P();
            _playbackStats();
            if (p.presentDirty)
            {
                p.presentDirty = false;
                _presentViewUpdate();
            }
            // The loupe's values are read back from what it last drew, which
            // is a frame behind a move; read them again until the button is
            // let go, so they settle on the pixel pointed at.
            if (p.probing)
            {
#if defined(FTK_SDL3)
                if (p.probeSDLWindow &&
                    (p.probeMotion.dx != 0.F || p.probeMotion.dy != 0.F))
                {
                    const ftk::V2F d(p.probeMotion.dx, p.probeMotion.dy);
                    p.probeMotion.dx = 0.F;
                    p.probeMotion.dy = 0.F;
                    _probeMove(d);
                }
#endif // FTK_SDL3
                _probeUpdate();
            }
#if defined(FTK_SDL3)
            if (Private::MouseMode::Shuttle == p.mouse.mode &&
                p.mouse.shuttleSDLWindow &&
                (p.shuttleMotion.dx != 0.F || p.shuttleMotion.dy != 0.F))
            {
                const ftk::V2F d(p.shuttleMotion.dx, p.shuttleMotion.dy);
                p.shuttleMotion.dx = 0.F;
                p.shuttleMotion.dy = 0.F;
                _shuttleMove(d * p.mouse.shuttleSDLScale);
            }
#endif // FTK_SDL3
        }

        void Viewport::_shuttleMove(const ftk::V2F& d)
        {
            FTK_P();
            // A frame for every 5.7 of the viewport's pixels; with Shift
            // held, twenty, for the fine adjustments, as in Blender. Read on
            // every move, so it can be pressed and let go during the drag
            // without a jump. A binding that takes Shift itself has no fine
            // speed.
            bool fine = false;
            if (!(static_cast<int>(p.frameShuttleBinding.modifier) &
                static_cast<int>(ftk::KeyModifier::Shift)))
            {
#if defined(FTK_SDL3)
                fine = SDL_GetModState() & SDL_KMOD_SHIFT;
#endif // FTK_SDL3
            }
            p.mouse.shuttleFrames += d.x * (fine ? .05 : .175) * p.frameShuttleScale;
            p.mouse.shuttleTravel += std::abs(d.x);

            // The mode is taken on the press whether or not there was a
            // player to read a position from, so the two can disagree and
            // the shuttle has nothing to move from.
            auto player = getPlayer();
            const int64_t offset = std::llround(p.mouse.shuttleFrames);
            if (player && p.mouse.shuttleStart.has_value() && offset != p.mouse.shuttleOffset)
            {
                p.mouse.shuttleOffset = offset;
                // Round the time range, as many times as the drag goes.
                const OTIO_NS::TimeRange& timeRange = player->getTimeRange();
                const double rate = timeRange.duration().rate();
                const double start = timeRange.start_time().rescaled_to(rate).value();
                const double duration = std::round(timeRange.duration().value());
                if (duration > 0.0)
                {
                    double f = std::fmod(
                        p.mouse.shuttleStart->rescaled_to(rate).value() - start + offset,
                        duration);
                    if (f < 0.0)
                    {
                        f += duration;
                    }
                    player->seek(OTIO_NS::RationalTime(start + f, rate));
                }
            }

#if defined(FTK_SDL3)
            if (SDL_Window* window = p.mouse.shuttleSDLWindow; window && !p.mouse.shuttleDrawn)
            {
                // The system's pointer, moved by the system and held in the
                // viewport: put on the other side when it reaches an edge.
                // Moving it does not count as motion: that is the mouse's
                // own.
                float gx = 0.F;
                float gy = 0.F;
                SDL_GetGlobalMouseState(&gx, &gy);
                int wx = 0;
                int wy = 0;
                SDL_GetWindowPosition(window, &wx, &wy);
                const float scale = p.mouse.shuttleSDLScale;
                ftk::V2I pos(
                    static_cast<int>(std::lround((gx - wx) * scale)),
                    static_cast<int>(std::lround((gy - wy) * scale)));
                const ftk::Box2I& g = getGeometry();
                const int margin = 2;
                if (g.w() > margin * 8 && g.h() > margin * 8)
                {
                    ftk::V2I warp = pos;
                    if (pos.x <= g.min.x + margin)
                    {
                        warp.x = g.max.x - margin - 1;
                    }
                    else if (pos.x >= g.max.x - margin)
                    {
                        warp.x = g.min.x + margin + 1;
                    }
                    if (pos.y <= g.min.y + margin)
                    {
                        warp.y = g.max.y - margin - 1;
                    }
                    else if (pos.y >= g.max.y - margin)
                    {
                        warp.y = g.min.y + margin + 1;
                    }
                    if (warp != pos)
                    {
                        SDL_WarpMouseGlobal(wx + warp.x / scale, wy + warp.y / scale);
                        ++p.mouse.shuttleWraps;
                        pos = warp;
                    }
                }
                // SDL's idea of where the pointer is, on it: SDL puts the
                // pointer back there when relative mode ends. In relative
                // mode this moves nothing.
                SDL_WarpMouseInWindow(window, pos.x / scale, pos.y / scale);
                if (pos != p.mouse.shuttleCursor)
                {
                    p.mouse.shuttleCursor = pos;
                    setDrawUpdate();
                }
            }
            else if (window)
            {
                // The pointer the shuttle draws: it follows the motion, and
                // the one that leaves the viewport comes back from the other
                // side.
                const ftk::Box2I& g = getGeometry();
                ftk::V2F& pointer = p.mouse.shuttlePointer;
                pointer.x += d.x;
                pointer.y += d.y;
                if (g.w() > 1 && g.h() > 1)
                {
                    const auto wrap = [](float& v, float min, float size)
                    {
                        if (v < min || v >= min + size)
                        {
                            v = min + std::fmod(std::fmod(v - min, size) + size, size);
                            return true;
                        }
                        return false;
                    };
                    const bool wrapX = wrap(pointer.x, g.min.x, g.w());
                    const bool wrapY = wrap(pointer.y, g.min.y, g.h());
                    if (wrapX || wrapY)
                    {
                        ++p.mouse.shuttleWraps;
                    }
                }
                // SDL's idea of where the pointer is, on the one drawn: it
                // puts the system's back there when relative mode ends. In
                // relative mode this moves nothing.
                const float scale = p.mouse.shuttleSDLScale;
                SDL_WarpMouseInWindow(window, pointer.x / scale, pointer.y / scale);
                p.mouse.shuttleCursor = ftk::V2I(
                    static_cast<int>(std::floor(pointer.x)),
                    static_cast<int>(std::floor(pointer.y)));
                setDrawUpdate();
            }
#endif // FTK_SDL3
        }

        void Viewport::_shuttleEnd()
        {
            FTK_P();
#if defined(FTK_SDL3)
            if (SDL_Window* window = p.mouse.shuttleSDLWindow)
            {
                SDL_RemoveEventWatch(&probeWatch, &p.shuttleMotion);
                const float scale = p.mouse.shuttleSDLScale;
                if (p.mouse.shuttleDrawn)
                {
                    // The system's pointer comes back where the drawn one
                    // is.
                    SDL_WarpMouseInWindow(
                        window,
                        p.mouse.shuttlePointer.x / scale,
                        p.mouse.shuttlePointer.y / scale);
                }
                else
                {
                    // The pointer stays where it is.
                    SDL_SetWindowMouseRect(window, nullptr);
                    float gx = 0.F;
                    float gy = 0.F;
                    SDL_GetGlobalMouseState(&gx, &gy);
                    int wx = 0;
                    int wy = 0;
                    SDL_GetWindowPosition(window, &wx, &wy);
                    SDL_WarpMouseInWindow(window, gx - wx, gy - wy);
                }
                SDL_SetWindowRelativeMouseMode(window, false);
                SDL_ResetHint(SDL_HINT_MOUSE_RELATIVE_SYSTEM_SCALE);
                SDL_ResetHint(SDL_HINT_MOUSE_RELATIVE_CURSOR_VISIBLE);
                SDL_ResetHint(SDL_HINT_MOUSE_RELATIVE_MODE_CENTER);
                p.mouse.shuttleSDLWindow = nullptr;
                setDrawUpdate();
                _log(ftk::Format("Shuttle ({0} pointer): {1} pixels, {2} wraps, {3} frames").
                    arg(p.mouse.shuttleDrawn ? "drawn" : "system").
                    arg(p.mouse.shuttleTravel, 0).
                    arg(p.mouse.shuttleWraps).
                    arg(p.mouse.shuttleOffset));
            }
#endif // FTK_SDL3
        }

        void Viewport::_playbackStats()
        {
            FTK_P();
            auto& stats = p.playbackStats;
            const auto player = getPlayer();
            const bool playing = player && player->getPlayback() != tl::Playback::Stop;
            const auto now = std::chrono::steady_clock::now();
            const auto report = [this, &stats, &player, now]
            {
                const std::chrono::duration<double> d = now - stats.start;
                if (d.count() < 1.0 || !player)
                {
                    return;
                }
                const double secs = d.count();
                const OTIO_NS::TimeRange& timeRange = player->getTimeRange();
                // Whether reading ahead was still at work beside playback.
                double cachedFrames = 0.0;
                for (const auto& range : _p->cacheInfo.video)
                {
                    cachedFrames += range.duration().rescaled_to(timeRange.duration().rate()).value();
                }
                const double cached = timeRange.duration().value() > 0.0 ?
                    std::min(100.0, cachedFrames / timeRange.duration().value() * 100.0) :
                    0.0;
                _log(ftk::Format(
                    "Playback at {0} of {1} fps, audio {2}: shown {3} fps ({4} skipped), "
                    "clock {5} fps ({6} jumps), {7} ticks a second, longest tick {8} ms, "
                    "longest between frames {9} ms, viewport drawn {10} times a second "
                    "in {11} ms, at most {12} ms, {13}% of the media cached").
                    arg(player->getSpeed(), 2).
                    arg(timeRange.duration().rate(), 2).
                    arg(player->getIOInfo().audio.isValid() ? "yes" : "no").
                    arg(stats.shown / secs, 2).
                    arg(stats.shownSkipped).
                    arg(stats.clockFrames / secs, 2).
                    arg(stats.clockJumps).
                    arg(stats.ticks / secs, 1).
                    arg(stats.maxTickGap * 1000.0, 1).
                    arg(stats.maxShownGap * 1000.0, 1).
                    arg(stats.draws / secs, 1).
                    arg(stats.draws > 0 ? stats.drawTotal / stats.draws * 1000.0 : 0.0, 1).
                    arg(stats.drawMax * 1000.0, 1).
                    arg(cached, 0));
            };
            if (playing && !stats.active)
            {
                stats = Private::PlaybackStats();
                stats.active = true;
                stats.start = now;
                stats.lastTick = now;
                stats.lastShownTime = now;
                return;
            }
            if (!playing)
            {
                if (stats.active)
                {
                    report();
                    stats.active = false;
                }
                return;
            }
            const std::chrono::duration<double> gap = now - stats.lastTick;
            stats.maxTickGap = std::max(stats.maxTickGap, gap.count());
            stats.lastTick = now;
            ++stats.ticks;
            if (now - stats.start >= std::chrono::seconds(5))
            {
                report();
                // The next five seconds, from where these left off.
                Private::PlaybackStats next;
                next.active = true;
                next.start = now;
                next.lastTick = now;
                next.lastShownTime = stats.lastShownTime;
                next.lastClock = stats.lastClock;
                next.lastShown = stats.lastShown;
                stats = next;
            }
        }

        void Viewport::_log(const std::string& text)
        {
            if (auto context = getContext())
            {
                context->getLogSystem()->print("djv::ui::Viewport", text);
            }
        }

        void Viewport::_probeMove(const ftk::V2F& d)
        {
            FTK_P();
            // What tells a click for the context menu from a drag.
            p.mouse.probeTravel += static_cast<int>(std::ceil(std::abs(d.x) + std::abs(d.y)));
            if (p.mouse.probeTravel > 2)
            {
                p.mouse.probeMoved = true;
            }
            p.probeVirtual.x += d.x;
            p.probeVirtual.y += d.y;
            const ftk::V2I target(
                static_cast<int>(std::lround(p.probeVirtual.x)),
                static_cast<int>(std::lround(p.probeVirtual.y)));
            _probeAt(target);
            if (p.probeWindowPos != target)
            {
                // Held at the edge of the picture: coming back starts from
                // the edge, not from wherever the hand went past it.
                p.probeVirtual = ftk::V2F(p.probeWindowPos.x, p.probeWindowPos.y);
            }
#if defined(FTK_SDL3)
            // SDL's own idea of where the hidden pointer is goes at the
            // mouse's speed, not the probe's, and the wheel goes to whatever
            // is under it: over the tools, the wheel scrolled them instead of
            // changing the average. Kept on the probe; in relative mode this
            // moves no pointer and sends no motion.
            if (p.probeSDLWindow)
            {
                SDL_WarpMouseInWindow(
                    p.probeSDLWindow,
                    p.probeWindowPos.x / p.probeSDLScale,
                    p.probeWindowPos.y / p.probeSDLScale);
            }
#endif // FTK_SDL3
            _probeUpdate();
        }

        void Viewport::_probeAt(const ftk::V2I& windowPos)
        {
            FTK_P();
            // The pixel under the pointer, held inside the picture: past an
            // edge the probe stays on the edge, so a corner is had by pushing
            // past it rather than by aiming at it, and there is nothing
            // outside the picture to measure anyway.
            ftk::V2I renderPos = toRenderPos(toViewportPos(windowPos));
            const ftk::Size2I renderSize = tl::getRenderSize(
                getCompareOptions(),
                p.binding->getDisplayOptions().aspectRatio,
                p.videoFrames);
            bool clamped = false;
            if (renderSize.isValid())
            {
                const ftk::V2I tmp(
                    std::clamp(renderPos.x, 0, renderSize.w - 1),
                    std::clamp(renderPos.y, 0, renderSize.h - 1));
                clamped = tmp != renderPos;
                renderPos = tmp;
            }
            // Back to the viewport, the middle of the pixel, which is where
            // the sample is taken and the loupe is put.
            ftk::V2I viewportPos = toViewportPos(windowPos);
            if (clamped)
            {
                const double zoom = getZoom();
                const ftk::V2I corner = fromRenderPos(renderPos);
                viewportPos = ftk::V2I(
                    corner.x + static_cast<int>(zoom / 2.0),
                    corner.y + static_cast<int>(zoom / 2.0));
            }
            sample(viewportPos);
            p.probeWindowPos = viewportPos + getGeometry().min;
            p.loupe->setTarget(renderPos);
            _loupeGeometry();
        }

        void Viewport::_loupeGeometry()
        {
            FTK_P();
            // Centered on the pointer: the middle of the loupe's picture is
            // the pixel under it, and the note sits above.
            const ftk::Size2I size = p.loupe->getLoupeSize();
            const int square = size.w;
            const int note = size.h - square;
            p.loupe->setGeometry(ftk::Box2I(
                p.probeWindowPos.x - square / 2,
                p.probeWindowPos.y - square / 2 - note,
                size.w,
                size.h));
        }

        void Viewport::_hudLayout()
        {
            FTK_P();
            auto viewportModel = p.viewportModel.lock();
            const auto options = viewportModel->getHUDOptions();
            // With no file there is nothing for the items to be about but
            // that there is no file: a placeholder for each, a black color
            // swatch, an empty cache bar, a zoom of an image that is not
            // there, reads as information (DJV #900). The options stay as
            // they are, and the items come back with the next file.
            const bool file = getPlayer() != nullptr;
            for (const auto& i : options.items)
            {
                const bool visible =
                    i.second != models::HUDPos::None &&
                    (file || models::HUDItem::FileName == i.first);
                p.hudWidgets[i.first]->setParent(visible ?
                    p.hudLayouts[i.second] :
                    nullptr);
            }
            for (const auto& i : p.hudLayouts)
            {
                i.second->setVisible(
                    p.hudActive &&
                    options.enabled &&
                    i.second->getChildren().size() > 0);
            }
        }
    }
}

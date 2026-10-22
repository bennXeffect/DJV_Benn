// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the DJV project.

#pragma once

#include <djv/UI/Export.h>

#include <tlRender/UI/Viewport.h>

namespace ftk
{
    class SysLogModel;
}

namespace djv
{
    namespace models
    {
        class AnnotationsModel;
        class ColorModel;
        class DrawModel;
        class FilesModel;
        class SettingsModel;
        class TimeUnitsModel;
        class ViewportModel;
    }

    namespace ui
    {
        //! Viewport.
        //! A sample of the picture: the pixel, its color as displayed and as
        //! stored in the file, averaged over a square of pixels around it.
        struct DJV_UI_API_TYPE PixelSample
        {
            ftk::V2I pixel;
            std::optional<ftk::Color4F> display;
            std::optional<ftk::Color4F> file;
            //! 0 for the pixel alone, 1 for 3x3, 2 for 5x5.
            int average = 0;

            bool operator == (const PixelSample&) const = default;
        };

        class DJV_UI_API_TYPE Viewport : public tl::ui::Viewport
        {
            FTK_NON_COPYABLE(Viewport);

        protected:
            void _init(
                const std::shared_ptr<ftk::Context>&,
                const std::shared_ptr<models::FilesModel>&,
                const std::shared_ptr<models::ColorModel>&,
                const std::shared_ptr<models::ViewportModel>&,
                const std::shared_ptr<models::TimeUnitsModel>&,
                const std::shared_ptr<models::SettingsModel>&,
                const std::shared_ptr<models::AnnotationsModel>&,
                const std::shared_ptr<models::DrawModel>&,
                const std::shared_ptr<ftk::SysLogModel>&,
                const std::shared_ptr<IWidget>& parent);

            Viewport();

        public:
            DJV_UI_API virtual ~Viewport();

            DJV_UI_API static std::shared_ptr<Viewport> create(
                const std::shared_ptr<ftk::Context>&,
                const std::shared_ptr<models::FilesModel>&,
                const std::shared_ptr<models::ColorModel>&,
                const std::shared_ptr<models::ViewportModel>&,
                const std::shared_ptr<models::TimeUnitsModel>&,
                const std::shared_ptr<models::SettingsModel>&,
                const std::shared_ptr<models::AnnotationsModel>&,
                const std::shared_ptr<models::DrawModel>&,
                const std::shared_ptr<ftk::SysLogModel>&,
                const std::shared_ptr<IWidget>& parent = nullptr);

            DJV_UI_API void setPlayer(const std::shared_ptr<tl::Player>&) override;

            //! Observe the last sample of the picture the probe took: the
            //! one color readout, for the HUD and the color picker tool.
            DJV_UI_API std::shared_ptr<ftk::IObservable<std::optional<PixelSample> > > observePixelSample() const;

            //! Set whether the toast stands in for the status bar.
            DJV_UI_API void setToastActive(bool);

            //! Show a short message in the toast's place, for a few seconds.
            //! Unlike the errors it shows even when the toast is not active:
            //! it is what to do next, e.g., how to leave presentation mode.
            DJV_UI_API void showHint(const std::string&);

            //! Set the present view, for presentation mode, as Blender's
            //! maximized editor shows a render: the picture fitted to the
            //! screen and centered, held there; zoomed in, it moves freely,
            //! and zoomed back out it is held again. It cannot be made
            //! smaller than the screen -- or, a picture smaller than the
            //! screen, than its own pixels, unless filled. The view before
            //! comes back after.
            DJV_UI_API void setPresentView(bool);

            //! Home in the present view: a picture smaller than the screen
            //! filled or at its own pixels, by turns; a larger one fitted
            //! again. False outside the present view.
            DJV_UI_API bool presentHome();

            //! Set whether the heads up display is shown at all. Separate from
            //! the display's own options so that turning it off for
            //! presentation does not forget what was being shown.
            DJV_UI_API void setHUDActive(bool);

            DJV_UI_API ftk::Size2I getSizeHint() const override;
            DJV_UI_API void setGeometry(const ftk::Box2I&) override;
            DJV_UI_API void drawEvent(const ftk::Box2I&, const ftk::DrawEvent&) override;
            DJV_UI_API void drawOverlayEvent(const ftk::Box2I&, const ftk::DrawEvent&) override;
            DJV_UI_API void mouseMoveEvent(ftk::MouseMoveEvent&) override;
            DJV_UI_API void mousePressEvent(ftk::MouseClickEvent&) override;
            DJV_UI_API void mouseReleaseEvent(ftk::MouseClickEvent&) override;
            DJV_UI_API void mouseEnterEvent(ftk::MouseEnterEvent&) override;
            DJV_UI_API void mouseLeaveEvent() override;
            DJV_UI_API void scrollEvent(ftk::ScrollEvent&) override;
            DJV_UI_API void tickEvent(bool, bool, const ftk::TickEvent&) override;

        private:
            //! Where a position in the viewport falls in the sources.
            struct SourceHit
            {
                //! Index into the active files, or -1 if the position is outside
                //! every source.
                int      index = -1;

                //! The position in the pixels of that source's image.
                ftk::V2F pos;

                //! Image pixels per render unit, for stroke widths.
                float    scale = 1.F;
            };

            //! Map a widget-local position to a source and its image pixels.
            SourceHit _hitTest(const ftk::V2I& widgetPos) const;

            //! Map a position in a source's image pixels back to widget-local
            //! coordinates.
            ftk::V2F _imageToWidget(int index, const ftk::V2F& imagePos) const;

            //! The boxes of the sources in render space, as used for drawing.
            std::vector<ftk::Box2I> _sourceBoxes() const;

            //! Whether a source's image is drawn in the current compare
            //! mode. Ink follows its image: strokes draw and hit-test only
            //! on sources that are shown.
            bool _sourceShown(int index) const;

            void _drawBegin(const ftk::V2I& widgetPos);
            void _drawContinue(const ftk::V2I& widgetPos);
            void _drawEnd();
            void _erase(const ftk::V2I& widgetPos);

            //! The cursor follows the drawing mode: a crosshair while the
            //! pointer is inside and drawing is enabled.
            void _cursorUpdate();

            void _toastUpdate();
            void _compareUpdate();
            void _hudUpdate();
            void _hudLayout();
            void _probeUpdate();
            void _loupeGeometry();
            void _probeAt(const ftk::V2I& windowPos);
            void _probeMove(const ftk::V2F&);
            void _presentViewUpdate();
            void _shuttleMove(const ftk::V2F&);
            void _shuttleEnd();
            void _log(const std::string&);
            void _playbackStats();

            FTK_PRIVATE();
        };
    }
}

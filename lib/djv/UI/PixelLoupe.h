// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the DJV project.

#pragma once

#include <djv/UI/Export.h>

#include <ftk/UI/IWidget.h>

#include <optional>

namespace tl
{
    class Player;
}

namespace djv
{
    namespace models
    {
        class ColorModel;
        class FilesModel;
        class ViewportModel;
    }

    namespace ui
    {
        //! The pixel loupe: a square view of the picture around a pixel,
        //! each pixel eight screen pixels wide, with a crosshair on the pixel
        //! and a box around the pixels averaged. It draws the picture the way
        //! the viewport does -- every layer, through the color management --
        //! so what it shows, and what is read back from it, is what is on
        //! screen at the size the file is.
        class DJV_UI_API_TYPE PixelLoupe : public ftk::IWidget
        {
            FTK_NON_COPYABLE(PixelLoupe);

        protected:
            void _init(
                const std::shared_ptr<ftk::Context>&,
                const std::shared_ptr<models::FilesModel>&,
                const std::shared_ptr<models::ColorModel>&,
                const std::shared_ptr<models::ViewportModel>&,
                const std::shared_ptr<IWidget>& parent);

            PixelLoupe();

        public:
            DJV_UI_API virtual ~PixelLoupe();

            //! Create a new widget.
            DJV_UI_API static std::shared_ptr<PixelLoupe> create(
                const std::shared_ptr<ftk::Context>&,
                const std::shared_ptr<models::FilesModel>&,
                const std::shared_ptr<models::ColorModel>&,
                const std::shared_ptr<models::ViewportModel>&,
                const std::shared_ptr<IWidget>& parent = nullptr);

            //! The magnification: a pixel of the picture is this many screen
            //! pixels across.
            static constexpr int zoom = 8;

            //! The number of pixels shown across, odd so one is in the
            //! middle.
            static constexpr int pixels = 19;

            //! Set the player.
            DJV_UI_API void setPlayer(const std::shared_ptr<tl::Player>&);

            //! Set the pixel in the middle, in the viewport's render
            //! coordinates.
            DJV_UI_API void setTarget(const ftk::V2I&);

            //! Set how far around the pixel is averaged: 0 the pixel alone,
            //! 1 three by three, 2 five by five.
            DJV_UI_API void setAverage(int);

            //! Set the averaged color to show around the pixels averaged.
            DJV_UI_API void setAverageColor(const std::optional<ftk::Color4F>&);

            //! Get the displayed color, averaged over the pixels the average
            //! takes in; read back from what was last drawn.
            DJV_UI_API std::optional<ftk::Color4F> getDisplayColor();

            //! Get the size of the loupe and the note above it, in screen
            //! pixels.
            DJV_UI_API ftk::Size2I getLoupeSize() const;

            DJV_UI_API ftk::Size2I getSizeHint() const override;
            DJV_UI_API void setGeometry(const ftk::Box2I&) override;
            DJV_UI_API void sizeHintEvent(const ftk::SizeHintEvent&) override;
            DJV_UI_API void drawEvent(const ftk::Box2I&, const ftk::DrawEvent&) override;
            DJV_UI_API void drawOverlayEvent(const ftk::Box2I&, const ftk::DrawEvent&) override;

        private:
            void _viewUpdate();

            FTK_PRIVATE();
        };
    }
}

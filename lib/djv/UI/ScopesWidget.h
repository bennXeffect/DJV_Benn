// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the DJV project.

#pragma once

#include <djv/UI/Export.h>

#include <ftk/UI/IWidget.h>

#include <array>
#include <functional>

#include <ftk/Core/Util.h>

namespace tl
{
    class Player;
}

namespace djv
{
    namespace models
    {
        class ColorModel;
    }

    namespace ui
    {
        //! What the scopes measure.
        enum class DJV_UI_API_TYPE ScopesSource
        {
            //! The picture as it is displayed, after color management --
            //! what Blender's scopes show.
            Display,

            //! The values stored in the file, before color management.
            File,

            Count,
            First = Display
        };
        FTK_ENUM(DJV_UI_API, ScopesSource);

        //! Scopes: a histogram, a waveform and a vectorscope of the current
        //! frame, as Blender's image editor has them.
        class DJV_UI_API_TYPE ScopesWidget : public ftk::IWidget
        {
            FTK_NON_COPYABLE(ScopesWidget);

        protected:
            void _init(
                const std::shared_ptr<ftk::Context>&,
                const std::shared_ptr<models::ColorModel>&,
                const std::shared_ptr<IWidget>& parent);

            ScopesWidget();

        public:
            DJV_UI_API virtual ~ScopesWidget();

            //! Create a new widget.
            DJV_UI_API static std::shared_ptr<ScopesWidget> create(
                const std::shared_ptr<ftk::Context>&,
                const std::shared_ptr<models::ColorModel>&,
                const std::shared_ptr<IWidget>& parent = nullptr);

            //! Set the player whose frames are measured.
            DJV_UI_API void setPlayer(const std::shared_ptr<tl::Player>&);

            //! Set what the scopes measure.
            DJV_UI_API void setSource(ScopesSource);

            //! Set the heights of the histogram, the waveform and the
            //! vectorscope, before the display scale. Each is dragged by the
            //! grip under it; the vectorscope stays square.
            DJV_UI_API void setHeights(const std::array<float, 3>&);

            //! Set the callback for when a height is dragged.
            DJV_UI_API void setHeightsCallback(const std::function<void(const std::array<float, 3>&)>&);

            //! Set which of the histogram, the waveform and the vectorscope
            //! are collapsed to their titles; a click on a title toggles it.
            DJV_UI_API void setCollapsed(const std::array<bool, 3>&);

            //! Set the callback for when a scope is collapsed or opened.
            DJV_UI_API void setCollapsedCallback(const std::function<void(const std::array<bool, 3>&)>&);

            DJV_UI_API ftk::Size2I getSizeHint() const override;
            DJV_UI_API void setGeometry(const ftk::Box2I&) override;
            DJV_UI_API void sizeHintEvent(const ftk::SizeHintEvent&) override;
            DJV_UI_API void tickEvent(bool, bool, const ftk::TickEvent&) override;
            DJV_UI_API void drawEvent(const ftk::Box2I&, const ftk::DrawEvent&) override;
            DJV_UI_API void mouseLeaveEvent() override;
            DJV_UI_API void mouseMoveEvent(ftk::MouseMoveEvent&) override;
            DJV_UI_API void mousePressEvent(ftk::MouseClickEvent&) override;
            DJV_UI_API void mouseReleaseEvent(ftk::MouseClickEvent&) override;

        private:
            void _processorUpdate();
            std::array<ftk::Box2I, 3> _boxes() const;
            int _gripAt(const ftk::V2I&) const;
            int _titleAt(const ftk::V2I&) const;

            FTK_PRIVATE();
        };
    }
}

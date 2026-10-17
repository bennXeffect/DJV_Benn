// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the DJV project.

#pragma once

#include <djv/App/Export.h>
#include <djv/App/IToolWidget.h>

namespace djv
{
    namespace models
    {
        struct LayerStack;
    }

    namespace app
    {
        //! Layers tool: the layers of the current layer stack, top first,
        //! with their visibility, order, offset and input color space.
        class DJV_APP_API_TYPE LayersTool : public IToolWidget
        {
            FTK_NON_COPYABLE(LayersTool);

        protected:
            void _init(
                const std::shared_ptr<ftk::Context>&,
                const std::shared_ptr<App>&,
                const std::shared_ptr<MainWindow>&,
                const std::shared_ptr<IWidget>& parent);

            LayersTool();

        public:
            DJV_APP_API virtual ~LayersTool();

            DJV_APP_API static std::shared_ptr<LayersTool> create(
                const std::shared_ptr<ftk::Context>&,
                const std::shared_ptr<App>&,
                const std::shared_ptr<MainWindow>&,
                const std::shared_ptr<IWidget>& parent = nullptr);

        private:
            void _apply(const models::LayerStack&);
            void _rowsUpdate();
            void _infoUpdate();

            FTK_PRIVATE();
        };
    }
}

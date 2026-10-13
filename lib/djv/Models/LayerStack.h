// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the DJV project.

#pragma once

#include <djv/Models/Export.h>

#include <tlRender/Core/Time.h>

#include <ftk/Core/Path.h>

#include <optional>
#include <string>
#include <vector>

namespace djv
{
    namespace models
    {
        //! The color space token for a layer whose picture already has the
        //! view applied -- a JPG or PNG saved "as render" from Blender. It
        //! resolves to the configuration's "View Inverse" color space for
        //! the current display and view (e.g. "AgX Base sRGB"), so the view
        //! applied to the composite gives the picture back as it was saved.
        DJV_MODELS_API const std::string& getLayerViewInverseToken();

        //! A layer of a layer stack: one piece of media, placed in time.
        struct DJV_MODELS_API_TYPE Layer
        {
            //! The name shown for the layer.
            std::string name;

            //! The media: an image sequence (with its frames), a still or a
            //! movie.
            ftk::Path path;

            //! The time range of the media, in its own time: the frames of a
            //! sequence, or what a movie reported when it was read.
            OTIO_NS::TimeRange timeRange;

            //! Whether the layer is shown.
            bool visible = true;

            //! Frames to move the layer by, later when positive.
            int64_t offset = 0;

            //! The input color space: empty for automatic, the view inverse
            //! token, or a color space of the configuration.
            std::string colorSpace;

            bool operator == (const Layer&) const = default;
        };

        //! A stack of layers composited over each other, the first at the
        //! bottom. Media is placed by its own frame numbers, so renders of
        //! the same shot line up without being told to, plus each layer's
        //! offset.
        struct DJV_MODELS_API_TYPE LayerStack
        {
            std::string name = "Layers";
            std::vector<Layer> layers;

            //! The frame rate of the stack.
            double rate = 24.0;

            bool operator == (const LayerStack&) const = default;
        };

        //! Write a layer stack as an OpenTimelineIO file: a video track per
        //! layer, the last on top, with the layer settings in the track
        //! metadata. Throws on error.
        DJV_MODELS_API void writeLayerStack(
            const std::string& fileName,
            const LayerStack&);

        //! Read a layer stack written by writeLayerStack(). Nothing when the
        //! file is not one.
        DJV_MODELS_API std::optional<LayerStack> readLayerStack(
            const std::string& fileName);

        //! Get whether a path names a layer stack file.
        DJV_MODELS_API bool isLayerStack(const ftk::Path&);

        //! Get a key for matching the paths of a piece of media, whichever
        //! frame of a sequence is named and however the separators go.
        DJV_MODELS_API std::string getMediaKey(const std::string& path);
    }
}

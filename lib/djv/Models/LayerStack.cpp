// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the DJV project.

#include <djv/Models/LayerStack.h>

#include <ftk/Core/Format.h>
#include <ftk/Core/String.h>

#include <opentimelineio/clip.h>
#include <opentimelineio/externalReference.h>
#include <opentimelineio/gap.h>
#include <opentimelineio/imageSequenceReference.h>
#include <opentimelineio/stack.h>
#include <opentimelineio/timeline.h>
#include <opentimelineio/track.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace djv
{
    namespace models
    {
        namespace
        {
            // Marks a timeline as a layer stack, and its version.
            const int64_t layerStackVersion = 1;

            std::string genericPath(const std::string& value)
            {
                std::string out = value;
                std::replace(out.begin(), out.end(), '\\', '/');
                return out;
            }

            // Where a layer starts on the stack's timeline: its first frame,
            // moved by its offset.
            int64_t layerStart(const Layer& layer)
            {
                return static_cast<int64_t>(std::llround(
                    layer.timeRange.start_time().value())) +
                    layer.offset;
            }
        }

        const std::string& getLayerViewInverseToken()
        {
            static const std::string out = "<view inverse>";
            return out;
        }

        void writeLayerStack(
            const std::string& fileName,
            const LayerStack& stack)
        {
            const double rate = stack.rate > 0.0 ? stack.rate : 24.0;

            // The stack starts where its earliest layer does, so the
            // timeline counts the frames the renders are numbered with.
            int64_t start = 0;
            bool first = true;
            for (const auto& layer : stack.layers)
            {
                const int64_t s = layerStart(layer);
                start = first ? s : std::min(start, s);
                first = false;
            }

            auto otioStack = new OTIO_NS::Stack;
            for (const auto& layer : stack.layers)
            {
                auto track = new OTIO_NS::Track(
                    layer.name,
                    std::nullopt,
                    OTIO_NS::Track::Kind::video);
                track->set_enabled(layer.visible);

                OTIO_NS::AnyDictionary djv;
                djv["path"] = genericPath(layer.path.get());
                if (layer.path.getFrames().has_value())
                {
                    djv["frameMin"] = static_cast<int64_t>(layer.path.getFrames()->min());
                    djv["frameMax"] = static_cast<int64_t>(layer.path.getFrames()->max());
                }
                djv["offset"] = layer.offset;
                djv["colorSpace"] = layer.colorSpace;
                track->metadata()["djv"] = djv;

                // The media's own range, in its own time. A layer that
                // starts later than the stack waits behind a gap.
                const double layerRate = layer.timeRange.duration().rate() > 0.0 ?
                    layer.timeRange.duration().rate() :
                    rate;
                const OTIO_NS::TimeRange range = layer.timeRange.duration().value() > 0.0 ?
                    layer.timeRange :
                    OTIO_NS::TimeRange(
                        OTIO_NS::RationalTime(0.0, layerRate),
                        OTIO_NS::RationalTime(1.0, layerRate));
                const int64_t gap = layerStart(layer) - start;
                if (gap > 0)
                {
                    track->append_child(new OTIO_NS::Gap(
                        OTIO_NS::RationalTime(static_cast<double>(gap), rate)));
                }

                auto clip = new OTIO_NS::Clip(layer.name);
                if (layer.path.isSeq() && layer.path.getFrames().has_value())
                {
                    const ftk::RangeI64 frames = layer.path.getFrames().value();
                    auto ref = new OTIO_NS::ImageSequenceReference(
                        genericPath(layer.path.getProtocol() + layer.path.getDir()),
                        layer.path.getBase(),
                        layer.path.getExt(),
                        static_cast<int>(frames.min()),
                        1,
                        layerRate,
                        layer.path.getPad(),
                        // Hold the last frame there is, so a stack built
                        // while a render is still running plays.
                        OTIO_NS::ImageSequenceReference::MissingFramePolicy::hold,
                        OTIO_NS::TimeRange(
                            OTIO_NS::RationalTime(static_cast<double>(frames.min()), layerRate),
                            OTIO_NS::RationalTime(
                                static_cast<double>(frames.max() - frames.min() + 1),
                                layerRate)));
                    clip->set_media_reference(ref);
                    clip->set_source_range(OTIO_NS::TimeRange(
                        OTIO_NS::RationalTime(static_cast<double>(frames.min()), layerRate),
                        OTIO_NS::RationalTime(
                            static_cast<double>(frames.max() - frames.min() + 1),
                            layerRate)));
                }
                else
                {
                    clip->set_media_reference(new OTIO_NS::ExternalReference(
                        genericPath(layer.path.get()),
                        range));
                    clip->set_source_range(range);
                }
                track->append_child(clip);
                otioStack->append_child(track);
            }

            auto timeline = new OTIO_NS::Timeline(stack.name);
            timeline->set_tracks(otioStack);
            timeline->set_global_start_time(
                OTIO_NS::RationalTime(static_cast<double>(start), rate));
            OTIO_NS::AnyDictionary djv;
            djv["layerStack"] = layerStackVersion;
            djv["rate"] = rate;
            timeline->metadata()["djv"] = djv;

            const OTIO_NS::SerializableObject::Retainer<OTIO_NS::Timeline> retainer(timeline);
            std::error_code ec;
            std::filesystem::create_directories(
                ftk::toFileSystem(fileName).parent_path(), ec);
            OTIO_NS::ErrorStatus errorStatus;
            if (!retainer->to_json_file(fileName, &errorStatus))
            {
                throw std::runtime_error(ftk::Format("Cannot write the layer stack \"{0}\": {1}").
                    arg(fileName).
                    arg(errorStatus.details));
            }
        }

        std::optional<LayerStack> readLayerStack(const std::string& fileName)
        {
            std::optional<LayerStack> out;
            OTIO_NS::ErrorStatus errorStatus;
            OTIO_NS::SerializableObject::Retainer<OTIO_NS::Timeline> timeline(
                dynamic_cast<OTIO_NS::Timeline*>(
                    OTIO_NS::Timeline::from_json_file(fileName, &errorStatus)));
            if (!timeline)
            {
                return out;
            }
            OTIO_NS::AnyDictionary djv;
            int64_t version = 0;
            if (!timeline->metadata().get_if_set("djv", &djv) ||
                !djv.get_if_set("layerStack", &version) ||
                version < 1)
            {
                return out;
            }
            LayerStack stack;
            stack.name = timeline->name();
            djv.get_if_set("rate", &stack.rate);
            for (const auto& track : timeline->video_tracks())
            {
                Layer layer;
                layer.name = track->name();
                layer.visible = track->enabled();
                OTIO_NS::AnyDictionary trackDjv;
                if (track->metadata().get_if_set("djv", &trackDjv))
                {
                    std::string path;
                    trackDjv.get_if_set("path", &path);
                    layer.path = ftk::Path(path);
                    int64_t frameMin = 0;
                    int64_t frameMax = 0;
                    if (trackDjv.get_if_set("frameMin", &frameMin) &&
                        trackDjv.get_if_set("frameMax", &frameMax))
                    {
                        layer.path.setFrames(ftk::RangeI64(frameMin, frameMax));
                    }
                    trackDjv.get_if_set("offset", &layer.offset);
                    trackDjv.get_if_set("colorSpace", &layer.colorSpace);
                }
                for (const auto& child : track->children())
                {
                    if (auto clip = dynamic_cast<OTIO_NS::Clip*>(child.value))
                    {
                        OTIO_NS::ErrorStatus clipError;
                        layer.timeRange = clip->trimmed_range(&clipError);
                        break;
                    }
                }
                stack.layers.push_back(layer);
            }
            out = stack;
            return out;
        }

        bool isLayerStack(const ftk::Path& path)
        {
            if (".otio" != ftk::toLower(path.getExt()))
            {
                return false;
            }
            // Cheaper than parsing the timeline: the marker is in the first
            // few lines of a file this writes, and a large timeline from
            // somewhere else is not read in whole to find it is not one.
            std::ifstream f(ftk::toFileSystem(path.get()));
            std::string line;
            for (int i = 0; i < 4000 && std::getline(f, line); ++i)
            {
                if (line.find("\"layerStack\"") != std::string::npos)
                {
                    return true;
                }
            }
            return false;
        }

        std::string getMediaKey(const std::string& value)
        {
            const ftk::Path path(genericPath(value));
            return ftk::toLower(
                path.getProtocol() + path.getDir() + path.getBase() + "#" + path.getExt());
        }
    }
}

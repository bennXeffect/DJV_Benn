// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the DJV project.

#include <djv/App/AppPrivate.h>
#include <djv/App/MainWindow.h>

#include <djv/Models/ColorModel.h>
#include <djv/Models/FilesModel.h>
#include <djv/Models/LayerStack.h>
#include <djv/Models/SettingsModel.h>

#include <tlRender/Timeline/Timeline.h>
#include <tlRender/Timeline/Util.h>

#include <ftk/UI/FileBrowser.h>
#include <ftk/Core/Format.h>
#include <ftk/Core/String.h>

#include <algorithm>
#include <filesystem>
#include <set>

namespace djv
{
    namespace app
    {
        namespace
        {
            // Pictures that already have the view applied: what Blender
            // writes to a JPG, a PNG or a movie when it saves "as render".
            bool isDisplayReferred(const ftk::Path& path)
            {
                static const std::set<std::string> exts =
                {
                    ".jpg", ".jpeg", ".png", ".bmp", ".tga", ".webp",
                    ".mp4", ".mov", ".mkv", ".avi", ".webm", ".m4v"
                };
                return exts.find(ftk::toLower(path.getExt())) != exts.end();
            }

            // Pictures with no alpha go below the ones with it: a render
            // preview at the bottom, its overlays above.
            bool isOpaque(const ftk::Path& path)
            {
                static const std::set<std::string> exts =
                {
                    ".jpg", ".jpeg", ".bmp",
                    ".mp4", ".mov", ".mkv", ".avi", ".webm", ".m4v"
                };
                return exts.find(ftk::toLower(path.getExt())) != exts.end();
            }
        }

        std::shared_ptr<ftk::IObservable<std::optional<models::LayerStack> > > App::observeLayerStack() const
        {
            return _p->layerStack;
        }

        std::filesystem::path App::_layerStacksDir() const
        {
            return getSettingsPath().parent_path() / "Layers";
        }

        std::optional<models::Layer> App::_makeLayer(
            const ftk::Path& path,
            const std::optional<OTIO_NS::TimeRange>& timeRange)
        {
            FTK_P();
            std::optional<models::Layer> out;
            models::Layer layer;
            layer.name = models::getDisplayName(path);
            layer.path = path;
            const double defaultRate = p.settingsModel->getImageSeq().io.defaultSpeed;
            if (path.isSeq() && path.getFrames().has_value())
            {
                const double rate = timeRange.has_value() ?
                    timeRange->duration().rate() :
                    defaultRate;
                const ftk::RangeI64 frames = path.getFrames().value();
                layer.timeRange = OTIO_NS::TimeRange(
                    OTIO_NS::RationalTime(static_cast<double>(frames.min()), rate),
                    OTIO_NS::RationalTime(static_cast<double>(frames.max() - frames.min() + 1), rate));
            }
            else if (timeRange.has_value())
            {
                layer.timeRange = timeRange.value();
            }
            else
            {
                // Read it to find out how long it is.
                try
                {
                    auto timeline = tl::Timeline::create(_context, path);
                    layer.timeRange = timeline->getTimeRange();
                }
                catch (const std::exception& e)
                {
                    _context->log("djv::app::App", e.what(), ftk::LogType::Error);
                    return out;
                }
            }
            if (isDisplayReferred(path))
            {
                layer.colorSpace = models::getLayerViewInverseToken();
            }
            out = layer;
            return out;
        }

        void App::_openLayerStack(models::LayerStack stack)
        {
            FTK_P();
            if (stack.layers.empty())
            {
                _context->log(
                    "djv::app::App",
                    "Nothing to stack: open the files to stack first.",
                    ftk::LogType::Warning);
                return;
            }
            std::stable_sort(
                stack.layers.begin(),
                stack.layers.end(),
                [](const models::Layer& a, const models::Layer& b)
                {
                    return isOpaque(a.path) && !isOpaque(b.path);
                });
            stack.rate = stack.layers.front().timeRange.duration().rate();

            // A name not in use yet.
            const std::filesystem::path dir = _layerStacksDir();
            std::filesystem::path fileName;
            for (int i = 1; i < 10000; ++i)
            {
                stack.name = ftk::Format("Layers {0}").arg(i);
                fileName = dir / (stack.name + ".otio");
                std::error_code ec;
                if (!std::filesystem::exists(fileName, ec))
                {
                    break;
                }
            }
            try
            {
                models::writeLayerStack(ftk::fromFileSystem(fileName), stack);
            }
            catch (const std::exception& e)
            {
                _context->log("djv::app::App", e.what(), ftk::LogType::Error);
                return;
            }
            std::optional<double> frame;
            if (auto player = p.player->get())
            {
                frame = player->getCurrentTime().value();
            }
            open(ftk::Path(ftk::fromFileSystem(fileName)), ftk::Path(), std::nullopt, false);
            // The stack counts the frames the layers are numbered with, so
            // the frame that was on screen is the same number in it.
            if (auto player = p.player->get(); player && frame.has_value())
            {
                player->seek(OTIO_NS::RationalTime(
                    frame.value(),
                    player->getTimeRange().duration().rate()));
            }
        }

        void App::newLayerStack()
        {
            FTK_P();
            models::LayerStack stack;
            for (const auto& item : p.filesModel->getFiles())
            {
                if (!item || models::isLayerStack(item->path))
                {
                    continue;
                }
                if (auto layer = _makeLayer(item->path, item->timeRange))
                {
                    stack.layers.push_back(layer.value());
                }
            }
            _openLayerStack(stack);
        }

        void App::addLayersDialog()
        {
            FTK_P();
            ftk::FileBrowserOpenOptions options;
            options.title = "Add Layers";
            options.multiple = true;
            auto fileBrowserSystem = _context->getSystem<ftk::FileBrowserSystem>();
            fileBrowserSystem->open(
                p.mainWindow,
                [this](const std::vector<ftk::Path>& value)
                {
                    addLayers(value);
                },
                options);
        }

        void App::addLayers(const std::vector<ftk::Path>& paths)
        {
            FTK_P();
            // Gathered into their sequences, once each however many of a
            // sequence's frames were chosen.
            ftk::DirListOptions dirListOptions;
            dirListOptions.seqExts = tl::getExts(_context, static_cast<int>(tl::FileType::Seq));
            dirListOptions.seqMaxDigits = p.settingsModel->getImageSeq().maxDigits;
            dirListOptions.seq = true;
            std::vector<models::Layer> layers;
            std::set<std::string> keys;
            for (const auto& path : paths)
            {
                const std::string key = models::getMediaKey(path.get());
                if (path.hasNum() && !keys.insert(key).second)
                {
                    continue;
                }
                for (const auto& i : tl::getPaths(_context, path, dirListOptions))
                {
                    if (auto layer = _makeLayer(i, std::nullopt))
                    {
                        layers.push_back(layer.value());
                    }
                }
            }
            if (layers.empty())
            {
                return;
            }
            auto stack = p.layerStack->get();
            if (stack.has_value())
            {
                // On top of what is there.
                stack->layers.insert(stack->layers.end(), layers.begin(), layers.end());
                setLayerStack(stack.value());
            }
            else
            {
                models::LayerStack newStack;
                newStack.layers = layers;
                _openLayerStack(newStack);
            }
        }

        void App::saveLayerStackDialog()
        {
            FTK_P();
            auto a = p.filesModel->getA();
            if (!a || !models::isLayerStack(a->path))
            {
                return;
            }
            const std::string from = a->path.get();
            ftk::FileBrowserOpenOptions options;
            options.title = "Save Layer Stack";
            options.mode = ftk::FileBrowserMode::Save;
            options.fileName = a->path.getFileName();
            options.extensions.push_back(".otio");
            options.extensionsLabel = "Layer stacks";
            auto fileBrowserSystem = _context->getSystem<ftk::FileBrowserSystem>();
            fileBrowserSystem->open(
                p.mainWindow,
                [this, from](const ftk::Path& value)
                {
                    std::string fileName = value.getFileName(true);
                    if (".otio" != ftk::toLower(value.getExt()))
                    {
                        fileName += ".otio";
                    }
                    // The layers name their media by absolute paths, so the
                    // file is the same wherever it goes.
                    std::error_code ec;
                    std::filesystem::copy_file(
                        ftk::toFileSystem(from),
                        ftk::toFileSystem(fileName),
                        std::filesystem::copy_options::overwrite_existing,
                        ec);
                    if (ec)
                    {
                        _context->log(
                            "djv::app::App",
                            ftk::Format("Cannot save the layer stack \"{0}\": {1}").
                                arg(fileName).
                                arg(ec.message()),
                            ftk::LogType::Error);
                        return;
                    }
                    closeFile();
                    open(ftk::Path(fileName), ftk::Path(), std::nullopt, false);
                },
                options);
        }

        void App::setLayerStack(const models::LayerStack& value)
        {
            FTK_P();
            auto a = p.filesModel->getA();
            if (!a || !models::isLayerStack(a->path))
            {
                return;
            }
            try
            {
                models::writeLayerStack(a->path.get(), value);
            }
            catch (const std::exception& e)
            {
                _context->log("djv::app::App", e.what(), ftk::LogType::Error);
                return;
            }
            reload();
            _layerStackUpdate();
        }

        void App::_layerStackUpdate()
        {
            FTK_P();
            std::optional<models::LayerStack> stack;
            if (!p.activeFiles.empty() &&
                p.activeFiles.front() &&
                models::isLayerStack(p.activeFiles.front()->path))
            {
                stack = models::readLayerStack(p.activeFiles.front()->path.get());
            }
            std::map<std::string, std::string> colorSpaces;
            if (stack.has_value())
            {
                for (const auto& layer : stack->layers)
                {
                    if (!layer.colorSpace.empty())
                    {
                        colorSpaces[models::getMediaKey(layer.path.get())] = layer.colorSpace;
                    }
                }
            }
            p.colorModel->setLayerColorSpaces(colorSpaces);
            _thumbnailColorUpdate();
            p.layerStack->setIfChanged(stack);
        }
    }
}

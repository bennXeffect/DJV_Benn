// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the DJV project.

#include <djv/Models/BlenderConfig.h>

#include <ftk/Core/Path.h>
#include <ftk/Core/String.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>

namespace djv
{
    namespace models
    {
        namespace
        {
            // How deep a folder holding installations is searched: enough
            // for "Blender Foundation\Blender 4.2" and for a launcher's
            // "library\stable\blender-5.2.2-lts...", without walking a
            // whole drive when someone picks its root.
            const int searchDepth = 3;

            bool isVersionDir(const std::string& name)
            {
                static const std::regex re("^[0-9]+\\.[0-9]+$");
                return std::regex_match(name, re);
            }

            std::filesystem::path configIn(const std::filesystem::path& versionDir)
            {
                return versionDir / "datafiles" / "colormanagement" / "config.ocio";
            }

            bool isFile(const std::filesystem::path& path)
            {
                std::error_code ec;
                return std::filesystem::is_regular_file(path, ec);
            }

            bool isDir(const std::filesystem::path& path)
            {
                std::error_code ec;
                return std::filesystem::is_directory(path, ec);
            }

            std::string capitalize(const std::string& value)
            {
                std::string out = value;
                if (!out.empty())
                {
                    out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[0])));
                }
                return out;
            }

            // A name for the installation from its folder: the official
            // archives and the Blender Launcher name it
            // "blender-5.2.2-lts.d13f752e3b9c", the installer
            // "Blender 4.2".
            std::string getLabel(
                const std::filesystem::path& installDir,
                const std::string& version)
            {
                const std::string name = ftk::fromFileSystem(installDir.filename());
                static const std::regex archive(
                    "^blender-([0-9]+\\.[0-9]+(\\.[0-9]+)?)(-([a-zA-Z]+))?([-+.].*)?$",
                    std::regex::icase);
                std::smatch m;
                if (std::regex_match(name, m, archive))
                {
                    std::string out = "Blender " + m[1].str();
                    if (m[4].matched)
                    {
                        const std::string kind = ftk::toLower(m[4].str());
                        out += " " + ("lts" == kind ? std::string("LTS") : capitalize(kind));
                    }
                    return out;
                }
                std::string out = name;
                if (out.find(version) == std::string::npos)
                {
                    out += " (" + version + ")";
                }
                return out;
            }

            void addInstall(
                const std::filesystem::path& installDir,
                std::vector<BlenderOCIOConfig>& out)
            {
                std::error_code ec;
                for (const auto& entry : std::filesystem::directory_iterator(installDir, ec))
                {
                    const std::string name = ftk::fromFileSystem(entry.path().filename());
                    if (isVersionDir(name))
                    {
                        const std::filesystem::path config = configIn(entry.path());
                        if (isFile(config))
                        {
                            BlenderOCIOConfig item;
                            item.version = name;
                            item.installDir = ftk::fromFileSystem(installDir);
                            item.configPath = ftk::fromFileSystem(config);
                            item.label = getLabel(installDir, name);
                            out.push_back(item);
                        }
                    }
                }
            }

            void search(
                const std::filesystem::path& dir,
                int depth,
                std::vector<BlenderOCIOConfig>& out)
            {
                const size_t size = out.size();
                addInstall(dir, out);
                if (out.size() > size || depth <= 0)
                {
                    return;
                }
                std::error_code ec;
                for (const auto& entry : std::filesystem::directory_iterator(
                    dir,
                    std::filesystem::directory_options::skip_permission_denied,
                    ec))
                {
                    if (isDir(entry.path()))
                    {
                        search(entry.path(), depth - 1, out);
                    }
                }
            }

            std::vector<int> versionNumbers(const BlenderOCIOConfig& value)
            {
                // The label carries the full version where the folder name
                // gave one; the data version otherwise.
                std::vector<int> out;
                static const std::regex re("[0-9]+");
                const std::string s = value.label + " " + value.version;
                for (auto i = std::sregex_iterator(s.begin(), s.end(), re);
                    i != std::sregex_iterator() && out.size() < 3;
                    ++i)
                {
                    out.push_back(std::atoi(i->str().c_str()));
                }
                return out;
            }

            // The library folder of the Blender Launcher
            // (github.com/Victor-IX/Blender-Launcher-V2), which keeps its
            // builds in "stable", "daily", "experimental" and "custom"
            // folders under it.
            std::vector<std::filesystem::path> getBlenderLauncherDirs()
            {
                std::vector<std::filesystem::path> out;
                std::vector<std::filesystem::path> iniFiles;
#if defined(_WIN32)
                if (const char* env = std::getenv("LOCALAPPDATA"))
                {
                    iniFiles.push_back(
                        ftk::toFileSystem(env) / "Blender Launcher" / "Blender Launcher.ini");
                }
#else
                if (const char* env = std::getenv("HOME"))
                {
                    iniFiles.push_back(
                        std::filesystem::path(env) / ".config" / "Blender Launcher" / "Blender Launcher.ini");
                }
#endif
                for (const auto& iniFile : iniFiles)
                {
                    std::ifstream f(iniFile);
                    std::string line;
                    while (std::getline(f, line))
                    {
                        const std::string key = "library_folder=";
                        if (0 == line.compare(0, key.size(), key))
                        {
                            std::string value = line.substr(key.size());
                            // Qt writes backslashes doubled.
                            std::string unescaped;
                            for (size_t i = 0; i < value.size(); ++i)
                            {
                                if ('\\' == value[i] && i + 1 < value.size() && '\\' == value[i + 1])
                                {
                                    ++i;
                                }
                                unescaped.push_back(value[i]);
                            }
                            while (!unescaped.empty() &&
                                ('\r' == unescaped.back() || ' ' == unescaped.back()))
                            {
                                unescaped.pop_back();
                            }
                            if (!unescaped.empty())
                            {
                                const std::filesystem::path library =
                                    ftk::toFileSystem(unescaped);
                                for (const char* kind : { "stable", "daily", "experimental", "custom" })
                                {
                                    out.push_back(library / kind);
                                }
                            }
                        }
                    }
                }
                return out;
            }

            bool isPreRelease(const BlenderOCIOConfig& value)
            {
                const std::string label = ftk::toLower(value.label);
                for (const char* kind : { "alpha", "beta", "rc", "daily", "experimental" })
                {
                    if (label.find(kind) != std::string::npos)
                    {
                        return true;
                    }
                }
                return false;
            }

            void sortAndUnique(std::vector<BlenderOCIOConfig>& out)
            {
                std::set<std::string> seen;
                std::vector<BlenderOCIOConfig> unique;
                for (const auto& i : out)
                {
                    const std::string key = ftk::toLower(i.configPath);
                    if (seen.insert(key).second)
                    {
                        unique.push_back(i);
                    }
                }
                // Releases first, newest first; then the alphas and betas,
                // which are for trying things rather than for work.
                std::stable_sort(
                    unique.begin(),
                    unique.end(),
                    [](const BlenderOCIOConfig& a, const BlenderOCIOConfig& b)
                    {
                        const bool aPre = isPreRelease(a);
                        const bool bPre = isPreRelease(b);
                        if (aPre != bPre)
                        {
                            return bPre;
                        }
                        return versionNumbers(a) > versionNumbers(b);
                    });
                out = unique;
            }
        }

        std::vector<BlenderOCIOConfig> findBlenderOCIOConfigs(const std::string& value)
        {
            std::vector<BlenderOCIOConfig> out;
            if (value.empty())
            {
                return out;
            }
            std::filesystem::path path = ftk::toFileSystem(value);
            if (isFile(path))
            {
                if (".ocio" == ftk::toLower(ftk::fromFileSystem(path.extension())))
                {
                    // The configuration itself. Inside an installation it is
                    // named after it; anywhere else after its folder.
                    BlenderOCIOConfig item;
                    item.configPath = ftk::fromFileSystem(path);
                    const std::filesystem::path versionDir =
                        path.parent_path().parent_path().parent_path();
                    const std::string version =
                        ftk::fromFileSystem(versionDir.filename());
                    if (isVersionDir(version))
                    {
                        item.version = version;
                        item.installDir = ftk::fromFileSystem(versionDir.parent_path());
                        item.label = getLabel(versionDir.parent_path(), version);
                    }
                    else
                    {
                        item.label = ftk::fromFileSystem(path.parent_path().filename());
                    }
                    out.push_back(item);
                    return out;
                }
                // blender.exe, or any other file of the installation.
                path = path.parent_path();
            }
            if (!isDir(path))
            {
                return out;
            }

            // The version folder, "5.2".
            const std::string name = ftk::fromFileSystem(path.filename());
            if (isVersionDir(name) && isFile(configIn(path)))
            {
                BlenderOCIOConfig item;
                item.version = name;
                item.installDir = ftk::fromFileSystem(path.parent_path());
                item.configPath = ftk::fromFileSystem(configIn(path));
                item.label = getLabel(path.parent_path(), name);
                out.push_back(item);
                return out;
            }

            search(path, searchDepth, out);
            sortAndUnique(out);
            return out;
        }

        std::vector<BlenderOCIOConfig> findInstalledBlenderOCIOConfigs(
            const std::vector<std::string>& extraDirs)
        {
            std::vector<std::filesystem::path> dirs;
#if defined(_WIN32)
            for (const char* var : { "ProgramFiles", "ProgramW6432", "ProgramFiles(x86)" })
            {
                if (const char* env = std::getenv(var))
                {
                    const std::filesystem::path base = ftk::toFileSystem(env);
                    dirs.push_back(base / "Blender Foundation");
                    dirs.push_back(base / "Steam" / "steamapps" / "common" / "Blender");
                }
            }
#elif defined(__APPLE__)
            dirs.push_back("/Applications/Blender.app/Contents/Resources");
#else
            dirs.push_back("/usr/share/blender");
            dirs.push_back("/opt");
            if (const char* env = std::getenv("HOME"))
            {
                dirs.push_back(std::filesystem::path(env) / ".steam" / "steam" / "steamapps" / "common" / "Blender");
            }
#endif
            for (const auto& dir : getBlenderLauncherDirs())
            {
                dirs.push_back(dir);
            }
            std::vector<BlenderOCIOConfig> out;
            for (const auto& dir : dirs)
            {
                if (isDir(dir))
                {
                    search(dir, searchDepth, out);
                }
            }
            for (const auto& dir : extraDirs)
            {
                const auto found = findBlenderOCIOConfigs(dir);
                out.insert(out.end(), found.begin(), found.end());
            }
            sortAndUnique(out);
            return out;
        }

        std::string getBlenderDefaultView()
        {
            return "AgX";
        }
    }
}

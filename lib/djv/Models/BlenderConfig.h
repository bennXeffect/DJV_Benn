// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the DJV project.

#pragma once

#include <djv/Models/Export.h>

#include <string>
#include <vector>

namespace djv
{
    namespace models
    {
        //! The OpenColorIO configuration of a Blender installation.
        struct DJV_MODELS_API_TYPE BlenderOCIOConfig
        {
            //! A name for menus, e.g. "Blender 5.2.2 LTS".
            std::string label;

            //! The data version, the name of the folder holding the
            //! configuration, e.g. "5.2".
            std::string version;

            //! The installation folder, the one holding blender.exe.
            std::string installDir;

            //! The configuration file.
            std::string configPath;

            bool operator == (const BlenderOCIOConfig&) const = default;
        };

        //! Find the Blender configurations under a path. The path can be the
        //! configuration itself, blender.exe, an installation folder, its
        //! version folder ("5.2"), or a folder holding installations (for
        //! example "Blender Foundation", or the library folder of the
        //! Blender Launcher), searched a few levels deep. Newest first.
        DJV_MODELS_API std::vector<BlenderOCIOConfig> findBlenderOCIOConfigs(
            const std::string& path);

        //! Find the Blender configurations in the usual places -- Program
        //! Files, Steam -- and in the given folders. Newest first, without
        //! duplicates.
        DJV_MODELS_API std::vector<BlenderOCIOConfig> findInstalledBlenderOCIOConfigs(
            const std::vector<std::string>& extraDirs);

        //! Get the view Blender gives new scenes ("AgX"), used when one of
        //! its configurations is chosen and the display has that view.
        DJV_MODELS_API std::string getBlenderDefaultView();
    }
}

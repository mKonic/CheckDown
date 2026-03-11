-- premake5.lua - CheckDown Download Manager

-- Configure these paths for your system, or set environment variables
local qt_dir   = os.getenv("QT_DIR")   or "C:/dev/c++/CheckDown/deps/qt/6.8.3/msvc2022_64"
local curl_dir = os.getenv("CURL_DIR") or "C:/dev/c++/CheckDown/deps/curl-install"

workspace "CheckDown"
    configurations { "Debug", "Release" }
    architecture "x86_64"
    location "build"
    startproject "CheckDown"

    cppdialect "C++latest"
    language "C++"

project "CheckDown"
    kind "WindowedApp"
    targetdir "bin/%{cfg.buildcfg}"
    objdir "obj/%{cfg.buildcfg}"

    files {
        "src/**.h",
        "src/**.cpp",
        "generated/**.cpp",
        "generated/qrc_resources.cpp",
        "resources/checkdown.rc",   -- embeds app icon into the PE (Windows Explorer / taskbar)
    }

    includedirs {
        "src",
        "generated",
        "vendor/json/include",
        qt_dir .. "/include",
        qt_dir .. "/include/QtCore",
        qt_dir .. "/include/QtGui",
        qt_dir .. "/include/QtWidgets",
        qt_dir .. "/include/QtNetwork",
        curl_dir .. "/include",
    }

    libdirs {
        qt_dir .. "/lib",
        curl_dir .. "/lib",
    }

    -- MOC + RCC are handled by scripts/build.py before premake runs.
    -- No prebuild commands needed here.

    filter "configurations:Debug"
        defines { "DEBUG", "_DEBUG", "QT_DEBUG" }
        symbols "On"
        runtime "Debug"
        links {
            "Qt6Cored", "Qt6Guid", "Qt6Widgetsd", "Qt6Networkd", "Qt6EntryPointd",
            "libcurl",
        }
        postbuildcommands {
            '{COPYFILE} "' .. qt_dir .. '/bin/Qt6Cored.dll" "%{cfg.targetdir}"',
            '{COPYFILE} "' .. qt_dir .. '/bin/Qt6Guid.dll" "%{cfg.targetdir}"',
            '{COPYFILE} "' .. qt_dir .. '/bin/Qt6Widgetsd.dll" "%{cfg.targetdir}"',
            '{COPYFILE} "' .. qt_dir .. '/bin/Qt6Networkd.dll" "%{cfg.targetdir}"',
        }

    filter "configurations:Release"
        defines { "NDEBUG", "QT_NO_DEBUG" }
        optimize "On"
        runtime "Release"
        links {
            "Qt6Core", "Qt6Gui", "Qt6Widgets", "Qt6Network", "Qt6EntryPoint",
            "libcurl",
        }
        postbuildcommands {
            '{COPYFILE} "' .. qt_dir .. '/bin/Qt6Core.dll" "%{cfg.targetdir}"',
            '{COPYFILE} "' .. qt_dir .. '/bin/Qt6Gui.dll" "%{cfg.targetdir}"',
            '{COPYFILE} "' .. qt_dir .. '/bin/Qt6Widgets.dll" "%{cfg.targetdir}"',
            '{COPYFILE} "' .. qt_dir .. '/bin/Qt6Network.dll" "%{cfg.targetdir}"',
        }

    filter "system:windows"
        systemversion "latest"
        buildoptions { "/Zc:__cplusplus", "/utf-8" }
        defines {
            "_WIN32_WINNT=0x0A00",
            "WIN32_LEAN_AND_MEAN",
            "NOMINMAX",
            "CURL_STATICLIB",
        }
        links {
            "ws2_32", "wldap32", "crypt32", "normaliz", "advapi32",
        }
        entrypoint "mainCRTStartup"
        -- Qt platform plugin + yt-dlp binary directories
        postbuildcommands {
            '{MKDIR} "%{cfg.targetdir}/platforms"',
            '{MKDIR} "%{cfg.targetdir}/vendor/yt-dlp"',
            '{COPYFILE} "%{wks.location}/../vendor/yt-dlp/yt-dlp.exe" "%{cfg.targetdir}/vendor/yt-dlp/yt-dlp.exe"',
        }

    -- Copy platform plugin per config
    filter { "system:windows", "configurations:Debug" }
        postbuildcommands {
            '{COPYFILE} "' .. qt_dir .. '/plugins/platforms/qwindowsd.dll" "%{cfg.targetdir}/platforms"',
        }

    filter { "system:windows", "configurations:Release" }
        postbuildcommands {
            '{COPYFILE} "' .. qt_dir .. '/plugins/platforms/qwindows.dll" "%{cfg.targetdir}/platforms"',
        }

    filter {}

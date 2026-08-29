# Qt6 launcher. Disable with -DBUILD_LAUNCHER=OFF.
# find_package picks up Ubuntu/Debian qt6-base-dev, Homebrew qt6, and the
# official Qt SDK on Windows.

find_package(Qt6 6.5 COMPONENTS Widgets REQUIRED)

set(CMAKE_AUTOMOC ON)

set(LAUNCHER_SRCS
    main.cpp
    MainWindow.cpp
)

set(LAUNCHER_HDRS
    MainWindow.hpp
)

source_group(launcher FILES ${LAUNCHER_SRCS} ${LAUNCHER_HDRS})

# Windows: link as a GUI app so no console window pops up alongside it.
if(WIN32)
    set(LAUNCHER_GUI_TYPE WIN32)
endif()

add_executable(opendf-launcher
    ${LAUNCHER_GUI_TYPE}
    ${LAUNCHER_SRCS} ${LAUNCHER_HDRS}
)

target_link_libraries(opendf-launcher PRIVATE Qt6::Widgets)

# Put the launcher beside opendf and bsatool: it looks for the engine next to
# itself, so they have to be siblings in the build tree just as they are once
# installed. Setting only the base RUNTIME_OUTPUT_DIRECTORY keeps this correct
# under multi-config generators too -- they append the per-config subdirectory
# to it, which is exactly what the top-level targets get by defaulting to the
# build root, so the three stay together in either layout.
set_target_properties(opendf-launcher PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${opendf_BINARY_DIR}"
)

# Fallback used when writing settings.cfg: where the shaders and MyGUI media
# live if they aren't found next to the installed binary. In a build tree
# that's the source dir; DATA_PATH is what the engine's own settings.cfg.in
# is configured with, so the two agree.
target_compile_definitions(opendf-launcher PRIVATE
    OPENDF_DATA_DIR="${DATA_PATH}/data"
)

install(TARGETS opendf-launcher RUNTIME DESTINATION bin)


# Optional: build innoextract alongside the launcher.
#
# The launcher shells out to innoextract to unpack DaggerfallSetup, which is an
# Inno Setup installer. Distro packages are all innoextract 1.9 (2020), which
# only understands Inno Setup up to 6.0.5 and fails on current DaggerfallSetup
# builds (6.6.x); the fix lives in a fork until it is upstreamed. Turning this
# on builds that fork next to the launcher, so a working innoextract is found
# without touching the system.
#
# It is off by default because innoextract pulls in Boost, liblzma and iconv,
# which most people building just the engine do not want. Without it the
# launcher still finds an innoextract on PATH, and falls back to Wine.
option(BUILD_INNOEXTRACT "Build a bundled innoextract for the launcher" OFF)

# Which innoextract to build. This points at a fork rather than upstream
# because no released innoextract can read the installer DaggerfallSetup now
# ships: the only release (1.9, 2020) stops at Inno Setup 6.0.5, upstream
# master reaches 6.4.0, and DaggerfallSetup 3.2.0 is built with 6.6.1. The
# branch adds 6.4-6.6 support and is filed upstream as dscharrer/innoextract#214
# -- switch these back to upstream once that lands.
set(INNOEXTRACT_REPOSITORY "https://github.com/psi29a/innoextract.git"
    CACHE STRING "Git repository to fetch innoextract from")
set(INNOEXTRACT_TAG "inno-6.6-loader-rev2"
    CACHE STRING "Git tag/branch/commit of innoextract to build")

if(BUILD_INNOEXTRACT)
    include(FetchContent)

    # innoextract installs its own binary, man page and completions; we only
    # want the executable, staged beside the launcher.
    set(INNOEXTRACT_SKIP_INSTALL ON CACHE BOOL "" FORCE)

    # INNOEXTRACT_TAG names a branch, so re-check it on every configure --
    # a shallow clone of a moving branch is otherwise pinned to whatever it
    # first fetched. GIT_SHALLOW keeps that cheap.
    FetchContent_Declare(innoextract
        GIT_REPOSITORY   "${INNOEXTRACT_REPOSITORY}"
        GIT_TAG          "${INNOEXTRACT_TAG}"
        GIT_SHALLOW      TRUE
        UPDATE_DISCONNECTED FALSE
    )
    FetchContent_MakeAvailable(innoextract)

    if(TARGET innoextract)
        # Sit beside the launcher so QStandardPaths::findExecutable finds it
        # from the application directory at runtime.
        set_target_properties(innoextract PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${opendf_BINARY_DIR}"
        )
        add_dependencies(opendf-launcher innoextract)
        install(PROGRAMS "$<TARGET_FILE:innoextract>" DESTINATION bin)
    endif()
endif()

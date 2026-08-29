# 3.16 is the floor Qt6's own config files require, and FetchContent_MakeAvailable
# (used by the optional bundled innoextract) needs 3.14.
cmake_minimum_required(VERSION 3.16)

project(opendf)

# Prefer the GLVND OpenGL libraries over the legacy libGL when both are present.
if(POLICY CMP0072)
    cmake_policy(SET CMP0072 NEW)
endif()
set(OpenGL_GL_PREFERENCE GLVND)

set(CMAKE_MODULE_PATH "${opendf_SOURCE_DIR}/cmake" "${CMAKE_MODULE_PATH}")

include(CheckCXXCompilerFlag)

# MyGUI 3.4+ headers use std::string_view, so C++17 is the minimum.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_definitions("-DNOMINMAX")
    set(CMAKE_EXE_LINKER_FLAGS "/FORCE:Multiple ${CMAKE_EXE_LINKER_FLAGS}")
else()
    check_cxx_compiler_flag("-Wall" HAVE_WALL_SWITCH)
    if(HAVE_WALL_SWITCH)
        add_definitions("-Wall")
    endif()
    check_cxx_compiler_flag("-Wextra" HAVE_WEXTRA_SWITCH)
    if(HAVE_WEXTRA_SWITCH)
        add_definitions("-Wextra")
    endif()
    # MyGUI triggers a lot of unused parameter warnings...
    check_cxx_compiler_flag("-Wno-unused-parameter" HAVE_WNO_UNUSED_PARAMETER_SWITCH)
    if(HAVE_WNO_UNUSED_PARAMETER_SWITCH)
        add_definitions("-Wno-unused-parameter")
    endif()
endif()


# AUTO builds the launcher when Qt6 is available and quietly skips it when it
# isn't, so configuring the engine alone doesn't fail on a machine with no Qt6
# development packages. ON demands it and errors out if Qt6 is missing; OFF
# never builds it.
set(BUILD_LAUNCHER AUTO CACHE STRING "Build the OpenDF launcher (ON/OFF/AUTO)")
set_property(CACHE BUILD_LAUNCHER PROPERTY STRINGS AUTO ON OFF)

if(BUILD_LAUNCHER STREQUAL "AUTO")
    find_package(Qt6 6.5 QUIET COMPONENTS Widgets)
    if(Qt6_FOUND)
        set(BUILD_LAUNCHER ON)
    else()
        message(STATUS "Qt6 6.5 Widgets not found -- not building the launcher")
        set(BUILD_LAUNCHER OFF)
    endif()
endif()

find_package(OpenSceneGraph REQUIRED osgDB osgViewer osgGA osgUtil)
find_package(SDL2 REQUIRED)
find_package(OpenGL REQUIRED)
find_package(MyGUI REQUIRED)

# MyGUI's EventPair is either a CompositeEvent (obsolete delegates enabled) or a
# bare Event, depending on MYGUI_DONT_USE_OBSOLETE. That choice changes the layout
# of every widget, so it must match how libMyGUIEngine itself was built or event
# offsets silently disagree and attaching a delegate corrupts memory.
#
# MyGUI installs nothing that records which way it was built, so this cannot be
# detected -- it has to be told. ON matches the distro packages we target
# (Debian/Ubuntu build MyGUI with -DMYGUI_DONT_USE_OBSOLETE=TRUE); turn it OFF
# if you link against a MyGUI built with its own upstream default of FALSE.
option(MYGUI_DONT_USE_OBSOLETE
       "Match a MyGUI built with MYGUI_DONT_USE_OBSOLETE=TRUE" ON)
if(MYGUI_DONT_USE_OBSOLETE)
    add_definitions("-DMYGUI_DONT_USE_OBSOLETE")
endif()

include_directories("${opendf_SOURCE_DIR}/src")

set(SRCS src/components/sdlutil/graphicswindow.cpp
         src/components/settings/configfile.cpp
         src/components/archives/archive.cpp
         src/components/archives/bsaarchive.cpp
         src/components/vfs/manager.cpp
         src/components/vfs/osg_callbacks.cpp
         src/components/resource/texturemanager.cpp
         src/components/resource/meshmanager.cpp
         src/components/mygui_osg/rendermanager.cpp
         src/components/mygui_osg/texture.cpp
         src/components/mygui_osg/vertexbuffer.cpp
         src/components/mygui_osg/datamanager.cpp
         src/components/dfosg/texloader.cpp
         src/components/dfosg/meshloader.cpp
         src/opendf/input/input.cpp
         src/opendf/render/pipeline.cpp
         src/opendf/render/renderer.cpp
         src/opendf/gui/gui.cpp
         src/opendf/class/animated.cpp
         src/opendf/class/placeable.cpp
         src/opendf/class/activator.cpp
         src/opendf/actions/linker.cpp
         src/opendf/actions/mover.cpp
         src/opendf/actions/door.cpp
         src/opendf/actions/exitdoor.cpp
         src/opendf/actions/unknown.cpp
         src/opendf/world/world.cpp
         src/opendf/world/pitems.cpp
         src/opendf/world/ditems.cpp
         src/opendf/world/mblocks.cpp
         src/opendf/world/dblocks.cpp
         src/opendf/log.cpp
         src/opendf/cvars.cpp
         src/opendf/engine.cpp
         src/opendf/main.cpp
)

set(HDRS src/misc/sparsearray.hpp
         src/components/sdlutil/graphicswindow.hpp
         src/components/settings/configfile.hpp
         src/components/archives/archive.hpp
         src/components/archives/bsaarchive.hpp
         src/components/vfs/manager.hpp
         src/components/vfs/osg_callbacks.hpp
         src/components/resource/texturemanager.hpp
         src/components/resource/meshmanager.hpp
         src/components/mygui_osg/diagnostic.h
         src/components/mygui_osg/rendermanager.h
         src/components/mygui_osg/texture.h
         src/components/mygui_osg/vertexbuffer.h
         src/components/mygui_osg/datamanager.h
         src/components/dfosg/texloader.hpp
         src/components/dfosg/meshloader.hpp
         src/opendf/input/input.hpp
         src/opendf/render/pipeline.hpp
         src/opendf/render/renderer.hpp
         src/opendf/gui/iface.hpp
         src/opendf/gui/gui.hpp
         src/opendf/class/animated.hpp
         src/opendf/class/placeable.hpp
         src/opendf/class/activator.hpp
         src/opendf/actions/linker.hpp
         src/opendf/actions/mover.hpp
         src/opendf/actions/door.hpp
         src/opendf/actions/exitdoor.hpp
         src/opendf/actions/unknown.hpp
         src/opendf/world/iface.hpp
         src/opendf/world/world.hpp
         src/opendf/world/itembase.hpp
         src/opendf/world/pitems.hpp
         src/opendf/world/ditems.hpp
         src/opendf/world/mblocks.hpp
         src/opendf/world/dblocks.hpp
         src/opendf/log.hpp
         src/opendf/cvars.hpp
         src/opendf/engine.hpp
)

if(WIN32)
    set(SRCS src/misc/fnmatch.c
             ${SRCS}
    )

    set(HDRS src/misc/fnmatch.h
             ${HDRS}
    )
endif()

set(DATA_PATH "${opendf_SOURCE_DIR}")
configure_file("${opendf_SOURCE_DIR}/settings.cfg.in"
               "${opendf_BINARY_DIR}/settings.cfg")

add_executable(opendf ${SRCS} ${HDRS})
set_property(TARGET opendf APPEND PROPERTY INCLUDE_DIRECTORIES
    "${opendf_SOURCE_DIR}/src/opendf"
    ${OPENSCENEGRAPH_INCLUDE_DIRS}
    ${SDL2_INCLUDE_DIR}
    ${OPENGL_INCLUDE_DIR}
    ${MyGUI_INCLUDE_DIRS}
)
target_link_libraries(opendf
    ${OPENSCENEGRAPH_LIBRARIES}
    ${SDL2_LIBRARY}
    OpenGL::GL
    ${MyGUI_LIBRARIES}
)


set(SRCS src/components/archives/archive.cpp
         src/components/archives/bsaarchive.cpp
         src/bsatool/bsatool.cpp
)
set(HDRS src/components/archives/archive.hpp
         src/components/archives/bsaarchive.hpp
)

add_executable(bsatool ${SRCS} ${HDRS})


install(TARGETS opendf bsatool RUNTIME DESTINATION bin)


if(BUILD_LAUNCHER)
    add_subdirectory(src/launcher)
endif()

cmake_minimum_required(VERSION 3.14)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(EPS_VERSION_BASE 1.0.1) # This is used as fallback if getting from git fails. Must be only numeric.
# NOTE: also update hard-coded version number in
# - vcpkg.json
# This is ear-suite-extras' own version, independent of upstream EAR
# Production Suite's numbering (reset to 1.0.0 when this fork started,
# rather than continuing upstream's 1.2.2 - this repo has its own
# release history). Bump it here (and in vcpkg.json) when cutting a new
# GitHub Release; it ends up in the built plugins' own version field
# (visible in the DAW, e.g. via the plugin's info/about), and - if git
# tags are used (vX.Y.Z) - get_git_version.cmake picks up the tag
# automatically and this fallback is only used when no tag is reachable.

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake_modules")
include(get_git_version)
set(GIT_VERSION_BASE ${EPS_VERSION_BASE})
set(EPS_VERSION_CURRENT ${EPS_VERSION_BASE})
update_version_from_git(GIT_VERSION_BASE EPS_VERSION_CURRENT)
if(${GIT_VERSION_BASE} VERSION_GREATER ${EPS_VERSION_BASE} AND (NOT(EPS_PACKAGING)))
    message(WARNING "EPS_VERSION_BASE is lower than most recent git tag. Do you need to update it?")
endif()
set(EPS_VERSION_BASE ${GIT_VERSION_BASE})

if(APPLE)
  # JUCE's macOS plugin client sources include .mm (Objective-C++) files,
  # and juce_graphics pulls in the bundled plutovg SVG rasterizer as plain
  # C (.c) sources; modern CMake no longer auto-enables a language just
  # because such a source is present in a target, so they must be
  # requested explicitly here or the Ninja generator fails at generate
  # time with "CMAKE_<LANG>_COMPILE_OBJECT ... not set" for every target
  # that pulls in JUCE.
  project(ear-production-suite VERSION ${EPS_VERSION_BASE} LANGUAGES C CXX OBJC OBJCXX)
else()
  project(ear-production-suite VERSION ${EPS_VERSION_BASE} LANGUAGES CXX)
endif()

message(STATUS "\n-----------------------------------------------------------------------------------")
message(STATUS "   EAR Production Suite Base Version:      " ${EPS_VERSION_BASE})
message(STATUS "   EAR Production Suite Current Version:   " ${EPS_VERSION_CURRENT})
message(STATUS "-----------------------------------------------------------------------------------\n")

include(FeatureSummary)

option(EPS_USE_BAREBONES_PROFILE "Use the bare-bones version of the EBU Production Profile" OFF)
if(EPS_USE_BAREBONES_PROFILE)
	add_compile_definitions(BAREBONESPROFILE)
endif()

include(eps_set_install_paths)
eps_set_install_paths()

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

include(CTest)
option(BUILD_TESTING "Build tests" ON)
set(Boost_USE_STATIC_LIBS ON)
include(enable_msvc_static_runtime)
if(UNIX)
    find_package(Threads REQUIRED QUIET)
endif()
include(check_nng)
find_package(nng REQUIRED QUIET)
add_subdirectory(submodules)

set(EPS_SHARED_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shared)
set(JUCE_SUPPORT_RESOURCES ${CMAKE_CURRENT_SOURCE_DIR}/shared/resources)
add_subdirectory(${EPS_SHARED_DIR}/version)

set(EPS_PLUGIN_TARGETS "" CACHE INTERNAL "")
add_subdirectory(ear-production-suite-plugins)
# This is a trimmed fork (ear-suite-extras) carrying only custom_monitoring
# and object_osc: reaper-adm-extension, reaper-adm-export-source-plugin,
# the root tools/ (project-upgrade/setup wizard) and packaging/ (full-suite
# installer) from the upstream EAR Production Suite tree are unrelated to
# these two VST3 plugins and are not included here.

option(EPS_QUIET "Disable output of configuration summary" OFF)
if(NOT EPS_QUIET)
    message(STATUS "\n")
    feature_summary(WHAT ALL)
endif()

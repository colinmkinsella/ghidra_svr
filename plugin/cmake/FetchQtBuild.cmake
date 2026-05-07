# FetchQtBuild.cmake
#
# Clones Vector35's qt-build repo and registers a `build-qt` custom target
# that compiles Qt 6 with BN's patches and installs it to <repo-root>/qt/.
#
# This module runs at configure time only to clone the build scripts
# (~1 MB, fast).  It does NOT start the Qt compilation automatically —
# that step takes 1-2 hours and requires Poetry, CMake, Ninja, and a C++
# compiler.
#
# Typical first-time workflow on a machine without Qt:
#
#   cmake -B plugin/build -S plugin          # configure (this module clones qt-build)
#   cmake --build plugin/build --target build-qt   # compile Qt (~1-2 hours)
#   cmake -B plugin/build -S plugin          # re-configure now that Qt is present
#   cmake --build plugin/build               # build the plugin
#
# Subsequent configure runs find Qt in <repo-root>/qt/ and skip everything.
#
# To build against a different Qt, pass -DQt6_DIR=<path> to cmake and this
# module's target becomes a no-op.

include(FetchContent)

set(_qt_install_dir "${CMAKE_SOURCE_DIR}/../qt")

# If Qt6_DIR is already pointing somewhere valid, nothing to do here.
if(DEFINED Qt6_DIR AND EXISTS "${Qt6_DIR}/Qt6Config.cmake")
    return()
endif()

# Check whether a previous `build-qt` run already installed Qt here.
file(GLOB _local_qt "${_qt_install_dir}/*/lib/cmake/Qt6/Qt6Config.cmake")
if(_local_qt)
    list(GET _local_qt 0 _qt6cfg)
    get_filename_component(_qt6dir "${_qt6cfg}" DIRECTORY)
    set(Qt6_DIR "${_qt6dir}" CACHE PATH "Qt6 CMake directory" FORCE)
    message(STATUS "Qt6: using local build at ${Qt6_DIR}")
    return()
endif()

# ---- Clone qt-build (fast — only scripts, no Qt source yet) ----------------

FetchContent_Declare(qt-build
    GIT_REPOSITORY https://github.com/Vector35/qt-build.git
    GIT_TAG        main
    GIT_SHALLOW    TRUE
    SOURCE_DIR     "${CMAKE_SOURCE_DIR}/../qt-build"
    SUBBUILD_DIR   "${CMAKE_BINARY_DIR}/_deps/qt-build-subbuild"
)
FetchContent_GetProperties(qt-build)
if(NOT qt-build_POPULATED)
    message(STATUS "Qt6: cloning Vector35/qt-build...")
    FetchContent_Populate(qt-build)
endif()

set(_qtbuild_dir "${CMAKE_SOURCE_DIR}/../qt-build")

# ---- Select the platform build script --------------------------------------

if(WIN32)
    set(_build_script "${_qtbuild_dir}/build_win64.bat")
    set(_build_cmd    "${_build_script}")
elseif(APPLE)
    set(_build_script "${_qtbuild_dir}/build_macosx")
    set(_build_cmd    bash "${_build_script}")
else()
    # Detect Linux arm vs x86
    execute_process(COMMAND uname -m OUTPUT_VARIABLE _arch OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(_arch MATCHES "aarch64|arm")
        set(_build_script "${_qtbuild_dir}/build_linux-arm")
    else()
        set(_build_script "${_qtbuild_dir}/build_linux")
    endif()
    set(_build_cmd bash "${_build_script}")
endif()

# ---- Register the build-qt custom target -----------------------------------

# Pass QT_INSTALL_DIR so Qt is installed inside the repo rather than ~/Qt.
if(WIN32)
    set(_env_prefix ${CMAKE_COMMAND} -E env "QT_INSTALL_DIR=${_qt_install_dir}")
else()
    set(_env_prefix ${CMAKE_COMMAND} -E env QT_INSTALL_DIR=${_qt_install_dir})
endif()

add_custom_target(build-qt
    COMMAND ${_env_prefix} ${_build_cmd}
    WORKING_DIRECTORY "${_qtbuild_dir}"
    COMMENT "Building Qt 6 with Vector35 patches — this takes 1-2 hours. After it finishes, re-run cmake configure."
    VERBATIM
    USES_TERMINAL
)

message(STATUS "Qt6: not found. Run  cmake --build plugin/build --target build-qt  then re-run cmake configure.")
message(STATUS "Qt6: build scripts cloned to ${_qtbuild_dir}")
message(STATUS "Qt6: will install to ${_qt_install_dir}")

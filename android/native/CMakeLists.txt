# ares Android native build: the Mega Drive core (Genesis, Sega CD, 32X, Sega CD 32X),
# mia (media loaders) and the frontend in src/.
#
# Used two ways:
#  - by Gradle (android/app/build.gradle.kts) to build libares_android.so for the app;
#  - on a desktop Linux host to build `ares-headless`, a test harness that boots a game
#    with no window (cmake -S android/native -B build -DARES_ANDROID_HARNESS=ON).
#
# It reuses upstream's per-directory CMake files for thirdparty, nall, libco and the ares
# core, and supplies the few helpers they expect from upstream's top-level bootstrap.

cmake_minimum_required(VERSION 3.22)
project(ares_android C CXX)

get_filename_component(ARES_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

option(ARES_ANDROID_HARNESS "Build the headless test harness (desktop hosts)" OFF)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED TRUE)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_INCLUDE_CURRENT_DIR TRUE)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(
  -fno-strict-aliasing
  -Wno-switch -Wno-parentheses -Wno-unused -Wno-sign-compare -Wno-empty-body
  -Wno-deprecated-literal-operator -Wno-deprecated-declarations -Wno-unknown-warning-option
  -Wno-dangling-else -Wno-shift-op-parentheses -Wno-bitwise-op-parentheses -Wno-missing-braces
  -Wno-nullability-completeness -Wno-unused-command-line-argument
)

# --- what upstream's bootstrap normally provides -------------------------------------
list(APPEND CMAKE_MODULE_PATH "${ARES_ROOT}/cmake/common")
include(helpers_common)   # target_enable_feature, enable_core, ...
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/sourcery.cmake)

# replace the host-tool version with the configure-time one
function(add_sourcery_command target subdir)
  set(dir "${CMAKE_CURRENT_SOURCE_DIR}/${subdir}")
  ares_sourcery("${dir}/resource.bml" "${dir}/resource.cpp" "${dir}/resource.hpp")
endfunction()

set(OS_LINUX TRUE)  # nall / ares only use this to pick os-linux.cmake (harmless on Android)
set(ARES_NAME "ares")
set(ARES_WEBSITE "ares-emu.net")
set(ARES_VERSION "148-android")
set(ARES_LEGAL_COPYRIGHT_SHORT "2004-2026 ares team, Near")
set(ARES_ENABLE_CHD ON CACHE BOOL "" FORCE)
set(ARES_CORES md CACHE STRING "" FORCE)
set(ARES_UNITY_CORES ON CACHE BOOL "" FORCE)

# --- upstream libraries ---------------------------------------------------------------
add_subdirectory(${ARES_ROOT}/thirdparty ${CMAKE_BINARY_DIR}/thirdparty EXCLUDE_FROM_ALL)
add_subdirectory(${ARES_ROOT}/nall/nall ${CMAKE_BINARY_DIR}/nall EXCLUDE_FROM_ALL)
add_subdirectory(${ARES_ROOT}/libco ${CMAKE_BINARY_DIR}/libco EXCLUDE_FROM_ALL)
add_subdirectory(${ARES_ROOT}/ares ${CMAKE_BINARY_DIR}/ares EXCLUDE_FROM_ALL)
if(TARGET nall-static-compile-test)
  set_target_properties(nall-static-compile-test PROPERTIES EXCLUDE_FROM_ALL TRUE)
endif()

# --- mia as a library (no hiro user interface) ------------------------------------------
ares_sourcery("${ARES_ROOT}/mia/resource/resource.bml"
              "${ARES_ROOT}/mia/resource/resource.cpp"
              "${ARES_ROOT}/mia/resource/resource.hpp")
add_library(mia STATIC ${ARES_ROOT}/mia/mia.cpp ${ARES_ROOT}/mia/resource/resource.cpp)
target_compile_definitions(mia PRIVATE MIA_LIBRARY ARES_ENABLE_CHD)
target_include_directories(mia PRIVATE ${ARES_ROOT} ${ARES_ROOT}/thirdparty)
target_link_libraries(mia PRIVATE ares ares::nall::headers tzxfile chdr-static)

# --- the frontend ------------------------------------------------------------------------
add_library(ares_frontend STATIC src/frontend.cpp)
target_include_directories(ares_frontend PUBLIC src PRIVATE ${ARES_ROOT} ${ARES_ROOT}/thirdparty)
target_compile_definitions(ares_frontend PRIVATE MIA_LIBRARY ARES_ENABLE_CHD)
target_link_libraries(ares_frontend PUBLIC mia ares ares::nall::headers libco sljit chdr-static tzxfile ymfm qon)
# nall is an OBJECT library: link its objects straight into the final binaries, so that
# static libraries listed after them (sljit uses nall's executable-memory allocator) resolve.

if(ANDROID)
  add_library(ares_android SHARED src/jni.cpp src/gl-renderer.cpp src/audio-output.cpp)
  target_include_directories(ares_android PRIVATE src)
  target_link_libraries(ares_android PRIVATE nall ares_frontend android log GLESv2 aaudio)
  target_link_options(ares_android PRIVATE -Wl,--gc-sections -Wl,-z,max-page-size=16384)
endif()

if(ARES_ANDROID_HARNESS)
  add_executable(ares-headless src/harness.cpp)
  target_link_libraries(ares-headless PRIVATE nall ares_frontend)
endif()

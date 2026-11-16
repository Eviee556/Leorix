cmake_minimum_required(VERSION 3.24)
project(LeorixNative CXX C)

# Leorix Native - Milestone 1: app shell, Vulkan device, GPU/backend detection, 3D viewport grid.
# Build (Windows PowerShell, WinLibs MinGW):
#   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build
# Dependencies download automatically on the first configure (internet needed once, no git needed).

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(FETCHCONTENT_QUIET OFF)
include(FetchContent)

# ---------------------------------------------------------------- dependencies (pinned versions)
FetchContent_Declare(sdl3
  URL https://github.com/libsdl-org/SDL/archive/refs/tags/release-3.4.16.zip
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_Declare(vulkan_headers
  URL https://github.com/KhronosGroup/Vulkan-Headers/archive/refs/tags/v1.4.350.zip
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  SOURCE_SUBDIR download-only)
FetchContent_Declare(volk
  URL https://github.com/zeux/volk/archive/refs/tags/1.4.350.zip
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  SOURCE_SUBDIR download-only)
FetchContent_Declare(imgui
  URL https://github.com/ocornut/imgui/archive/545d1a884ab7be89bbab4600a649a206c8667773.zip
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  SOURCE_SUBDIR download-only) # download only; targets are defined below

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(tinyobj
  URL https://raw.githubusercontent.com/tinyobjloader/tinyobjloader/v2.0.0-rc4/tiny_obj_loader.h
  DOWNLOAD_NO_EXTRACT TRUE DOWNLOAD_DIR ${CMAKE_BINARY_DIR}/_deps/tinyobj-include SOURCE_SUBDIR download-only)
FetchContent_Declare(ufbx
  URL https://github.com/ufbx/ufbx/archive/refs/tags/v0.23.0.zip
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_Declare(tinygltf
  URL https://github.com/syoyo/tinygltf/archive/refs/tags/v2.9.6.zip
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE SOURCE_SUBDIR download-only)
FetchContent_Declare(lua
  URL https://github.com/lua/lua/archive/refs/tags/v5.4.7.zip
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE SOURCE_SUBDIR download-only)
FetchContent_Declare(stbwrite
  URL https://raw.githubusercontent.com/nothings/stb/2c980bb59875b0d32144a71867fbdebb2f77cd20/stb_image_write.h
  DOWNLOAD_NO_EXTRACT TRUE DOWNLOAD_DIR ${CMAKE_BINARY_DIR}/_deps/stbwrite-include SOURCE_SUBDIR download-only)
FetchContent_Declare(miniz
  URL https://github.com/richgel999/miniz/releases/download/3.0.2/miniz-3.0.2.zip
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE SOURCE_SUBDIR download-only)
FetchContent_Declare(stb
  URL https://raw.githubusercontent.com/nothings/stb/2c980bb59875b0d32144a71867fbdebb2f77cd20/stb_image.h
  DOWNLOAD_NO_EXTRACT TRUE
  DOWNLOAD_DIR ${CMAKE_BINARY_DIR}/_deps/stb-include
  SOURCE_SUBDIR download-only)
FetchContent_MakeAvailable(sdl3 vulkan_headers volk imgui stb stbwrite miniz tinyobj ufbx tinygltf lua)

# miniz: the .leorix container is a zip, per ENGINE_SPEC
add_library(miniz STATIC ${miniz_SOURCE_DIR}/miniz.c)
target_include_directories(miniz PUBLIC ${miniz_SOURCE_DIR})

# Lua 5.4 as a static lib via its single-translation-unit build.
add_library(lua STATIC ${lua_SOURCE_DIR}/onelua.c)
target_compile_definitions(lua PRIVATE MAKE_LIB)
target_include_directories(lua PUBLIC ${lua_SOURCE_DIR})

# ufbx single-source.
add_library(ufbx STATIC ${ufbx_SOURCE_DIR}/ufbx.c)
target_include_directories(ufbx PUBLIC ${ufbx_SOURCE_DIR})
if(NOT WIN32)
  target_link_libraries(ufbx PUBLIC m)
endif()

# volk loads vulkan-1.dll at runtime, so no Vulkan SDK install is needed to build or run.
add_library(volk STATIC ${volk_SOURCE_DIR}/volk.c)
target_include_directories(volk PUBLIC ${volk_SOURCE_DIR} ${vulkan_headers_SOURCE_DIR}/include)
if(WIN32)
  target_compile_definitions(volk PUBLIC VK_USE_PLATFORM_WIN32_KHR)
endif()

add_library(imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/imgui_demo.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_compile_definitions(imgui PUBLIC IMGUI_IMPL_VULKAN_USE_VOLK
  IMGUI_USER_CONFIG="${CMAKE_CURRENT_SOURCE_DIR}/src/imconfig_leorix.h")
target_link_libraries(imgui PUBLIC volk SDL3::SDL3-static)

# ---------------------------------------------------------------- Leorix
add_executable(leorix
  src/main.cpp
  src/gpu/VulkanContext.cpp
  src/render/Backends.cpp
  src/render/Material.cpp
  src/render/PathTracer.cpp
  src/render/HdriPresets.cpp
  src/anim/Armature.cpp
  src/anim/Animation.cpp
  src/paint/Paint.cpp
  src/paint/Project.cpp
  src/core/Undo.cpp
  src/viewport/Viewport3D.cpp
  src/app/Theme.cpp
  src/app/App.cpp
  src/app/App3D.cpp
  src/app/Icons.cpp
  src/gpu/Texture.cpp
  src/tools/BrushImage.cpp
  src/tools/Script.cpp
  src/tools/Sculpt.cpp
  src/anim/WeightPaint.cpp
  src/tools/Snap.cpp
  src/tools/VisualScript.cpp
  src/app/VisualScriptPanel.cpp
  src/app/RigPanel.cpp
  src/app/CurveEdit.cpp
  src/app/UVPaint.cpp
  src/app/WeightPaintPanel.cpp
  src/mesh/Mesh.cpp
  src/mesh/EditOps.cpp
  src/mesh/Primitives.cpp
  src/mesh/Scene.cpp
  src/mesh/IO.cpp
  src/mesh/UVView.cpp
  src/mesh/Unwrap.cpp
  src/mesh/UVEdit.cpp
  src/mesh/Modifier.cpp
  src/mesh/Curve.cpp
  src/mesh/Multires.cpp

  src/viewport/MeshView.cpp)
target_include_directories(leorix PRIVATE src
  ${CMAKE_BINARY_DIR}/_deps/stb-include
  ${CMAKE_BINARY_DIR}/_deps/stbwrite-include
  ${CMAKE_BINARY_DIR}/_deps/tinyobj-include
  ${tinygltf_SOURCE_DIR}
  ${miniz_SOURCE_DIR})
target_link_libraries(leorix PRIVATE imgui volk SDL3::SDL3-static lua ufbx miniz)
if(MINGW)
  # Single self-contained .exe: no libstdc++/libgcc DLLs to ship.
  target_link_options(leorix PRIVATE -static -static-libgcc -static-libstdc++)
endif()

enable_testing()
add_test(NAME selftest COMMAND leorix --selftest)

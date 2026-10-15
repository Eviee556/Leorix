#pragma once
// Leorix's Dear ImGui configuration.
// 32-bit vertex indices: the mesh viewport streams hundreds of thousands of triangles through
// ImDrawList, which overflows the default 16-bit index type (65,535 vertices) and asserts/corrupts.
// The Vulkan backend handles VK_INDEX_TYPE_UINT32 as long as this is set for every translation unit.
#define ImDrawIdx unsigned int

#include "paint/Project.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#include "stb_image.h"
#include "json.hpp"
#include "miniz.h"
#include <cstring>
#include <vector>

using nlohmann::json;

namespace {
const char* kMapKindIds[] = {"albedo", "normal", "roughness", "metallic", "ao",
                             "cavity", "height", "emissive", "opacity", "orm", "custom"};
const char* kBlendIds[] = {"normal", "multiply", "screen", "overlay", "darken", "lighten",
                           "color-dodge", "color-burn", "hard-light", "soft-light", "difference",
                           "exclusion", "hue", "saturation", "color", "luminosity"};

MapKind KindFromId(const std::string& s) {
    for (int i = 0; i < (int)MapKind::Count; ++i) if (s == kMapKindIds[i]) return (MapKind)i;
    return MapKind::Custom;
}
Blend BlendFromId(const std::string& s) {
    for (int i = 0; i < (int)Blend::Count; ++i) if (s == kBlendIds[i]) return (Blend)i;
    return Blend::Normal;
}

// stb_image_write into memory
void PngWriter(void* ctx, void* data, int size) {
    auto* out = static_cast<std::vector<unsigned char>*>(ctx);
    out->insert(out->end(), (unsigned char*)data, (unsigned char*)data + size);
}
bool EncodePng(const Layer& l, int w, int h, std::vector<unsigned char>& out) {
    out.clear();
    if (l.pixels.size() != (size_t)w * h * 4) return false;
    return stbi_write_png_to_func(PngWriter, &out, w, h, 4, l.pixels.data(), w * 4) != 0;
}
} // namespace

ProjectSaveInfo SaveLeorixProject(const std::string& path, PaintProject& project, const BrushSettings& brush,
                                  const std::string& blocks) {
    ProjectSaveInfo info;
    mz_zip_archive zip{};
    if (!mz_zip_writer_init_file(&zip, path.c_str(), 0)) { info.message = "Could not create the project file"; return info; }

    json manifest;
    manifest["format"] = "leorix";
    manifest["version"] = "1.0";
    manifest["generator"] = "Leorix Native";
    manifest["canvas"] = {{"width", project.width}, {"height", project.height},
                          {"colorSpace", "sRGB"}, {"bitDepth", 8}};
    manifest["project"] = {{"name", project.name}, {"activeMap", project.activeMap}};
    manifest["brush"] = {{"size", brush.size}, {"opacity", brush.opacity},
                         {"hardness", brush.hardness}, {"flow", brush.flow}, {"spacing", brush.spacing}};

    json maps = json::array();
    int layerId = 1;
    for (const MapDoc& m : project.maps) {
        json jm;
        jm["name"] = m.name;
        jm["kind"] = kMapKindIds[std::clamp((int)m.kind, 0, (int)MapKind::Count - 1)];
        jm["activeLayer"] = m.activeLayer;
        json jlayers = json::array();
        for (const Layer& l : m.layers) {
            std::vector<unsigned char> png;
            std::string file = "layers/L" + std::to_string(layerId) + "_" +
                               kMapKindIds[std::clamp((int)m.kind, 0, (int)MapKind::Count - 1)] + ".png";
            if (EncodePng(l, m.width, m.height, png)) {
                // MZ_BEST_SPEED: the PNG is already compressed, so deflate has little left to do
                mz_zip_writer_add_mem(&zip, file.c_str(), png.data(), png.size(), MZ_BEST_SPEED);
                ++info.layerFiles;
                info.bytes += png.size();
            }
            jlayers.push_back({{"id", layerId}, {"name", l.name}, {"opacity", l.opacity},
                               {"blend", kBlendIds[std::clamp((int)l.blend, 0, (int)Blend::Count - 1)]},
                               {"visible", l.visible}, {"locked", l.locked}, {"file", file}});
            ++layerId;
        }
        jm["layers"] = jlayers;
        maps.push_back(jm);
    }
    manifest["maps"] = maps;

    std::string text = manifest.dump(2);
    mz_zip_writer_add_mem(&zip, "manifest.json", text.data(), text.size(), MZ_BEST_COMPRESSION);
    if (!blocks.empty()) mz_zip_writer_add_mem(&zip, "blocks.json", blocks.data(), blocks.size(), MZ_BEST_COMPRESSION);

    mz_zip_writer_finalize_archive(&zip);
    mz_zip_writer_end(&zip);
    info.ok = true;
    info.message = "Saved " + std::to_string(info.layerFiles) + " layer image" +
                   (info.layerFiles == 1 ? "" : "s");
    return info;
}

ProjectSaveInfo LoadLeorixProject(const std::string& path, PaintProject& project,
                                  BrushSettings& brush, VulkanContext& vk, std::string* blocks) {
    ProjectSaveInfo info;
    mz_zip_archive zip{};
    if (!mz_zip_reader_init_file(&zip, path.c_str(), 0)) { info.message = "Not a readable .leorix file"; return info; }

    size_t size = 0;
    void* raw = mz_zip_reader_extract_file_to_heap(&zip, "manifest.json", &size, 0);
    if (!raw) { mz_zip_reader_end(&zip); info.message = "No manifest.json inside the project"; return info; }
    std::string text((const char*)raw, size);
    mz_free(raw);
    if (blocks) {
        blocks->clear();
        int at = mz_zip_reader_locate_file(&zip, "blocks.json", nullptr, 0);
        mz_zip_archive_file_stat st{};
        size_t bsize = 0;
        if (at >= 0 && mz_zip_reader_file_stat(&zip, (mz_uint)at, &st) && st.m_uncomp_size <= (16u << 20))   // refuse oversized entries
            if (void* b = mz_zip_reader_extract_to_heap(&zip, (mz_uint)at, &bsize, 0)) { blocks->assign((const char*)b, bsize); mz_free(b); }
    }

    json manifest;
    try { manifest = json::parse(text); }
    catch (const std::exception& e) { mz_zip_reader_end(&zip); info.message = std::string("Manifest is not valid JSON: ") + e.what(); return info; }

    // release the GPU textures of the project being replaced
    for (MapDoc& m : project.maps) m.texture.Destroy(vk);
    project.maps.clear();

    project.width = manifest.value("canvas", json::object()).value("width", 2048);
    project.height = manifest.value("canvas", json::object()).value("height", 2048);
    project.name = manifest.value("project", json::object()).value("name", std::string("untitled"));
    if (manifest.contains("brush")) {
        const json& b = manifest["brush"];
        brush.size = b.value("size", brush.size);
        brush.opacity = b.value("opacity", brush.opacity);
        brush.hardness = b.value("hardness", brush.hardness);
        brush.flow = b.value("flow", brush.flow);
        brush.spacing = b.value("spacing", brush.spacing);
    }

    for (const json& jm : manifest.value("maps", json::array())) {
        MapDoc m;
        m.kind = KindFromId(jm.value("kind", std::string("albedo")));
        m.name = jm.value("name", std::string(MapKindLabel(m.kind)));
        m.width = project.width; m.height = project.height;
        for (const json& jl : jm.value("layers", json::array())) {
            Layer l;
            l.name = jl.value("name", std::string("Layer"));
            l.opacity = jl.value("opacity", 100.0f);
            l.visible = jl.value("visible", true);
            l.locked = jl.value("locked", false);
            l.blend = BlendFromId(jl.value("blend", std::string("normal")));
            l.Fill(0, m.width, m.height);

            std::string file = jl.value("file", std::string());
            if (!file.empty()) {
                size_t sz = 0;
                void* png = mz_zip_reader_extract_file_to_heap(&zip, file.c_str(), &sz, 0);
                if (png) {
                    int w = 0, h = 0, ch = 0;
                    unsigned char* px = stbi_load_from_memory((const unsigned char*)png, (int)sz, &w, &h, &ch, 4);
                    mz_free(png);
                    if (px) {
                        if (w == m.width && h == m.height)
                            std::memcpy(l.pixels.data(), px, (size_t)w * h * 4);
                        stbi_image_free(px);
                        ++info.layerFiles;
                    }
                }
            }
            m.layers.push_back(std::move(l));
        }
        if (m.layers.empty()) { Layer bg; bg.name = "Background"; bg.Fill(MapKindFill(m.kind), m.width, m.height); m.layers.push_back(bg); }
        m.activeLayer = std::clamp(jm.value("activeLayer", 0), 0, (int)m.layers.size() - 1);
        m.dirty.Whole(m.width, m.height);
        project.maps.push_back(std::move(m));
    }
    mz_zip_reader_end(&zip);

    if (project.maps.empty()) project.New(project.width, project.height);
    project.activeMap = std::clamp(manifest.value("project", json::object()).value("activeMap", 0),
                                   0, (int)project.maps.size() - 1);
    info.ok = true;
    info.message = "Opened " + project.name + " - " + std::to_string(project.maps.size()) + " map(s), " +
                   std::to_string(info.layerFiles) + " layer image(s)";
    return info;
}

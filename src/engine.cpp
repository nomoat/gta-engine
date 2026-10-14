#include "engine.h"
#include "stream_wasm.h"
#include "json.h"
#include <cstring>
#include <fstream>
#include <sstream>

namespace engine {

namespace {

// Mesh asset binary format (little-endian):
//   u32 magic "MESH", u32 vertexCount, u32 indexCount,
//   vertexCount x (3 floats pos + 2 floats uv), indexCount x u32
bool decode_mesh(const std::vector<uint8_t>& data, Mesh& mesh) {
    if (data.size() < 12 || std::memcmp(data.data(), "MESH", 4) != 0) return false;
    size_t p = 4;
    auto u32 = [&]() -> uint32_t {
        uint32_t v = 0;
        for (int i = 0; i < 4; i++) v |= (uint32_t)data[p + i] << (8 * i);
        p += 4;
        return v;
    };
    auto f32 = [&]() -> float {
        uint32_t v = u32();
        float f; std::memcpy(&f, &v, 4);
        return f;
    };
    uint32_t vc = u32(), ic = u32();
    if (p + (size_t)vc * 5 * 4 + (size_t)ic * 4 > data.size()) return false;
    mesh.positions.reserve((size_t)vc * 3);
    mesh.uvs.reserve((size_t)vc * 2);
    for (uint32_t i = 0; i < vc; i++) {
        mesh.positions.push_back(f32());
        mesh.positions.push_back(f32());
        mesh.positions.push_back(f32());
        mesh.uvs.push_back(f32());
        mesh.uvs.push_back(f32());
    }
    for (uint32_t i = 0; i < ic; i++) mesh.indices.push_back(u32());
    return true;
}

// Texture asset binary format (little-endian):
//   u32 magic "TEX1", u32 width, u32 height, u32 channels(4), width*height*4 RGBA
bool decode_texture(const std::vector<uint8_t>& data, Texture& tex) {
    if (data.size() < 16 || std::memcmp(data.data(), "TEX1", 4) != 0) return false;
    size_t p = 4;
    auto u32 = [&]() -> uint32_t {
        uint32_t v = 0;
        for (int i = 0; i < 4; i++) v |= (uint32_t)data[p + i] << (8 * i);
        p += 4;
        return v;
    };
    tex.width = u32();
    tex.height = u32();
    uint32_t ch = u32();
    if (ch != 4) return false;
    size_t n = (size_t)tex.width * tex.height * 4;
    if (p + n > data.size()) return false;
    tex.rgba.assign(data.begin() + p, data.begin() + p + n);
    return true;
}

} // namespace

Engine::Engine(EngineConfig cfg) : cfg_(std::move(cfg)) {
#ifdef __EMSCRIPTEN__
    store_ = std::make_unique<EmscriptenStreamStore>(cfg_.dataRoot);
#else
    store_ = std::make_unique<FileStreamStore>(cfg_.dataRoot);
#endif
}

Engine::~Engine() = default;

bool Engine::init() {
    std::vector<uint8_t> text;
    if (!store_->readAll(cfg_.manifestFile, text)) {
        error_ = "cannot read manifest " + cfg_.manifestFile;
        return false;
    }
    std::string s(text.begin(), text.end());
    Manifest m;
    if (!parse_manifest(s, m)) {
        error_ = "bad manifest";
        return false;
    }

    // Prefetch every listed byte range, then parse the bundle header from the
    // first range of each bundle. (The header must be included in the manifest.)
    for (const auto& fp : m.files) {
        Bundle bundle;
        bool haveHeader = false;
        for (const auto& r : fp.ranges) {
            std::vector<uint8_t> chunk;
            if (!store_->read(fp.path, r.offset, r.length, chunk)) {
                error_ = "prefetch failed: " + fp.path;
                return false;
            }
            if (!haveHeader && chunk.size() >= 5 && std::memcmp(chunk.data(), "BNDL1", 5) == 0) {
                if (!parse_bundle_header(chunk.data(), chunk.size(), bundle)) {
                    error_ = "bad bundle header: " + fp.path;
                    return false;
                }
                haveHeader = true;
            }
        }
        if (!haveHeader) {
            error_ = "manifest lists no header range for bundle " + fp.path;
            return false;
        }
        bundles_.emplace_back(fp.path, std::move(bundle));
    }

    if (bundles_.empty()) {
        error_ = "manifest lists no bundles";
        return false;
    }
    return true;
}

bool Engine::load_asset(const std::string& bundlePath, const BundleEntry& entry,
                        std::vector<uint8_t>& out) {
    return store_->read(bundlePath, entry.offset, entry.length, out);
}

const Bundle& Engine::bundle(const std::string& path) const {
    for (const auto& b : bundles_)
        if (b.first == path) return b.second;
    static const Bundle empty;
    return empty;
}

bool Engine::load_demo_scene(Scene& scene) {
    const auto& b = bundles_.front();
    const BundleEntry* meshEntry = nullptr;
    const BundleEntry* texEntry = nullptr;
    for (const auto& e : b.second.entries) {
        if (e.type == AssetType::Mesh && !meshEntry) meshEntry = &e;
        if (e.type == AssetType::Texture && !texEntry) texEntry = &e;
    }
    if (!meshEntry || !texEntry) {
        error_ = "demo needs one mesh and one texture in the first bundle";
        return false;
    }
    std::vector<uint8_t> raw;
    if (!load_asset(b.first, *meshEntry, raw) || !decode_mesh(raw, scene.mesh)) {
        error_ = "mesh load failed";
        return false;
    }
    if (!load_asset(b.first, *texEntry, raw) || !decode_texture(raw, scene.texture)) {
        error_ = "texture load failed";
        return false;
    }
    return true;
}

bool Engine::run() {
    // Demo scene: first mesh + first texture in the first bundle.
    Scene scene;
    if (!load_demo_scene(scene)) return false;

    FrameBuffer fb{cfg_.fbWidth, cfg_.fbHeight, std::vector<uint8_t>((size_t)cfg_.fbWidth * cfg_.fbHeight * 4)};

    for (int i = 0; i < cfg_.frameCount; i++) {
        float t = (float)i / 30.0f;
        render_scene(scene, t, fb);
        char path[64];
        std::snprintf(path, sizeof(path), "frame_%03d.ppm", i);
        if (!write_ppm(fb, path)) {
            error_ = "write failed";
            return false;
        }
    }
    return true;
}

} // namespace engine

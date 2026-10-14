// Software rasterizer (CPU fallback for native development). It exists so the
// whole streaming pipeline can be exercised without a GPU. In the browser build
// the same Scene is handed to the WebGPU backend (web/wgpu_worker.js) instead.
#pragma once
#include <cstdint>
#include <vector>

namespace engine {

struct Mesh {
    // Interleaved positions (x,y,z) and UVs (u,v), then 32-bit indices.
    std::vector<float> positions; // xyz per vertex
    std::vector<float> uvs;       // uv per vertex
    std::vector<uint32_t> indices;
};

struct Texture {
    uint32_t width = 0, height = 0;
    std::vector<uint8_t> rgba; // width*height*4
};

struct Scene {
    Mesh mesh;
    Texture texture;
};

// A world: several meshes, each with its own texture. Used by the browser demo
// (ground + character) and uploaded to the GPU worker as separate objects.
struct WorldObject {
    Mesh mesh;
    Texture texture;
};

struct World {
    std::vector<WorldObject> objects;
};

struct FrameBuffer {
    uint32_t width = 0, height = 0;
    std::vector<uint8_t> rgba; // width*height*4, 0 = clear colour
};

// Renders the scene with a rotating model-view into `fb`. `t` is time in seconds.
void render_scene(const Scene& scene, float t, FrameBuffer& fb);

// Writes `fb` as a binary PPM (P6). Returns false on IO error.
bool write_ppm(const FrameBuffer& fb, const char* path);

} // namespace engine

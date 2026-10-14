// Engine core: application framework + asset streaming pipeline.
//
// This mirrors the GTA browser port's engine usage:
//   1. Parse the boot set manifest (data/bootset.json).
//   2. Prefetch only the listed byte ranges (the archive headers needed first).
//   3. Parse the bundle table from the prefetched header.
//   4. Stream each asset (mesh/texture) on demand by [offset, length).
//   5. Run the render loop.
//
// In the browser build the same loop runs on the loader.js "main runtime thread",
// IO goes through io_worker.js (HTTP Range) and rendering through wgpu_worker.js.
#pragma once
#include <memory>
#include <string>
#include "stream.h"
#include "manifest.h"
#include "bundle.h"
#include "raster.h"

namespace engine {

struct EngineConfig {
    std::string dataRoot;      // directory holding bootset.json + bundles (native)
    std::string manifestFile = "bootset.json";
    uint32_t fbWidth = 640;
    uint32_t fbHeight = 480;
    int frameCount = 60;       // frames to render in the native demo
};

class Engine {
public:
    explicit Engine(EngineConfig cfg);
    ~Engine();

    // Loads the manifest, prefetches headers, parses bundle tables.
    // Returns false and sets error() on failure.
    bool init();

    // Loads the first mesh + texture of the first bundle into `scene`.
    // Returns false and sets error() on failure.
    bool load_demo_scene(Scene& scene);

    // Loads every (mesh, texture) pair of the first bundle into `world`, paired
    // by order of appearance. Returns false and sets error() on failure.
    bool load_demo_world(World& world);

    // Runs the full demo (native): renders frameCount frames, writing PPMs.
    bool run();

    const std::string& error() const { return error_; }
    uint32_t fbWidth() const { return cfg_.fbWidth; }
    uint32_t fbHeight() const { return cfg_.fbHeight; }
    const Bundle& bundle(const std::string& path) const;

private:
    bool load_asset(const std::string& bundlePath, const BundleEntry& entry,
                    std::vector<uint8_t>& out);

    EngineConfig cfg_;
    std::unique_ptr<IStreamStore> store_;
    std::vector<FilePrefetch> prefetched_;      // manifest, flattened
    std::vector<std::pair<std::string, Bundle>> bundles_;
    std::string error_;
};

} // namespace engine

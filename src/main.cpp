// Entry point. Native: render frames to PPM. Emscripten: stream the scene, upload
// it to the GPU worker once, then drive a per-frame MVP matrix into a
// SharedArrayBuffer the GPU worker reads (engine-driven camera).
#include "engine.h"
#include "linmath.h"
#include <cstdio>
#include <string>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#include <emscripten/html5.h>

// Copies the loaded scene (mesh + texture) out of the wasm heap and posts it to
// the GPU worker (transferring the buffers so they aren't copied again).
EM_JS(void, js_gpu_scene,
      (const float* positions, const float* uvs, const uint32_t* indices,
       int vcount, int icount, const uint8_t* tex, int texW, int texH), {
    var pos = new Float32Array(vcount * 3);
    pos.set(HEAPF32.subarray(positions >> 2, (positions >> 2) + vcount * 3));
    var uv = new Float32Array(vcount * 2);
    uv.set(HEAPF32.subarray(uvs >> 2, (uvs >> 2) + vcount * 2));
    var idx = new Uint32Array(icount);
    idx.set(HEAPU32.subarray(indices >> 2, (indices >> 2) + icount));
    var texData = new Uint8Array(texW * texH * 4);
    texData.set(HEAPU8.subarray(tex, tex + texW * texH * 4));

    Module['gpuWorker'].postMessage({
        type: 'scene', positions: pos, uvs: uv, indices: idx,
        texW: texW, texH: texH, texData: texData,
    }, [pos.buffer, uv.buffer, idx.buffer, texData.buffer]);
});

// Writes a 4x4 matrix (column-major, 16 floats) into the shared frame state and
// bumps the frame counter with a release-style Atomics.store.
EM_JS(void, js_write_frame, (const float* mat, int frameNum), {
    if (!Module.__frameMat) {
        Module.__frameMat = new Float32Array(Module['frameState'], 0, 16);
        Module.__frameCounter = new Int32Array(Module['frameState'], 64, 1);
    }
    Module.__frameMat.set(HEAPF32.subarray(mat >> 2, (mat >> 2) + 16));
    Atomics.store(Module.__frameCounter, 0, frameNum);
});

// Reads the accumulated input deltas (and resets them) plus button/lock state.
EM_JS(void, js_read_input, (int* outDX, int* outDY, int* outWheel, int* outButtons, int* outLocked), {
    if (!Module.__inputI32)
        Module.__inputI32 = new Int32Array(Module['inputState'], 0, 8);
    var i = Module.__inputI32;
    // Read-and-clear the accumulated deltas with an atomic exchange.
    HEAP32[outDX >> 2] = Atomics.exchange(i, 0, 0);
    HEAP32[outDY >> 2] = Atomics.exchange(i, 1, 0);
    HEAP32[outWheel >> 2] = Atomics.exchange(i, 2, 0);
    HEAP32[outButtons >> 2] = Atomics.load(i, 3);
    HEAP32[outLocked >> 2] = Atomics.load(i, 4);
});

// Returns 1 if the key with the given keyCode is currently held down.
EM_JS(int, js_key_down, (int code), {
    if (!Module.__inputKeys)
        Module.__inputKeys = new Uint8Array(Module['inputState'], 32, 256);
    return Module.__inputKeys[code] ? 1 : 0;
});

static engine::Scene g_scene;
static float g_aspect = 640.0f / 480.0f;
static float g_yaw = 0.0f, g_pitch = 0.4f, g_dist = 3.0f;

static void em_main_loop() {
    static int frameNum = 0;

    int dx = 0, dy = 0, wheel = 0, buttons = 0, locked = 0;
    js_read_input(&dx, &dy, &wheel, &buttons, &locked);

    // Keyboard orbit (WASD / arrows) and zoom (Q/E).
    if (js_key_down(87) || js_key_down(38)) g_pitch += 0.03f; // W / Up
    if (js_key_down(83) || js_key_down(40)) g_pitch -= 0.03f; // S / Down
    if (js_key_down(65) || js_key_down(37)) g_yaw -= 0.03f;   // A / Left
    if (js_key_down(68) || js_key_down(39)) g_yaw += 0.03f;   // D / Right
    if (js_key_down(81)) g_dist += 0.05f;                     // Q zoom out
    if (js_key_down(69)) g_dist -= 0.05f;                     // E zoom in

    // Mouse-drag orbit (only while the pointer is locked).
    if (locked) {
        g_yaw += (float)dx * 0.005f;
        g_pitch += (float)dy * 0.005f;
    }
    g_dist -= (float)wheel * 0.15f;

    if (g_pitch > 1.5f) g_pitch = 1.5f;
    if (g_pitch < -1.5f) g_pitch = -1.5f;
    if (g_dist < 1.5f) g_dist = 1.5f;
    if (g_dist > 20.0f) g_dist = 20.0f;

    // Orbit camera: rotate around the origin, then pull back.
    math::Mat4 proj = math::Mat4::perspective(60.0f, g_aspect, 0.1f, 100.0f);
    math::Mat4 view = math::Mat4::rotateX(-g_pitch) * math::Mat4::rotateY(-g_yaw)
                    * math::Mat4::translate(0, 0, -g_dist);
    math::Mat4 mvp = proj * view;

    js_write_frame(mvp.m, ++frameNum);
}

int main(int argc, char** argv) {
    engine::EngineConfig cfg;
    cfg.dataRoot = argc > 1 ? argv[1] : "/data"; // served from web/data/

    engine::Engine engine(cfg);
    if (!engine.init()) {
        std::fprintf(stderr, "init failed: %s\n", engine.error().c_str());
        return 1;
    }
    if (!engine.load_demo_scene(g_scene)) {
        std::fprintf(stderr, "scene failed: %s\n", engine.error().c_str());
        return 1;
    }

    g_aspect = (float)engine.fbWidth() / (float)engine.fbHeight();

    int vcount = (int)(g_scene.mesh.positions.size() / 3);
    int icount = (int)g_scene.mesh.indices.size();
    js_gpu_scene(g_scene.mesh.positions.data(), g_scene.mesh.uvs.data(),
                 g_scene.mesh.indices.data(), vcount, icount,
                 g_scene.texture.rgba.data(), (int)g_scene.texture.width,
                 (int)g_scene.texture.height);

    std::fprintf(stderr, "scene uploaded (%d verts, %d indices); driving camera\n", vcount, icount);

    // The engine now drives a 60 fps camera loop, writing the MVP matrix each
    // frame. simulate_infinite_loop=1 keeps this worker blocked (GTA-style).
    emscripten_set_main_loop(em_main_loop, 60, 1);
    return 0;
}

#else // !__EMSCRIPTEN__

int main(int argc, char** argv) {
    engine::EngineConfig cfg;
    cfg.dataRoot = argc > 1 ? argv[1] : "data";

    engine::Engine e(cfg);
    if (!e.init()) {
        std::fprintf(stderr, "init failed: %s\n", e.error().c_str());
        return 1;
    }
    std::printf("initialized ok\n");
    if (!e.run()) {
        std::fprintf(stderr, "run failed: %s\n", e.error().c_str());
        return 1;
    }
    std::printf("done: wrote %d frames to frame_*.ppm\n", cfg.frameCount);
    return 0;
}

#endif // __EMSCRIPTEN__

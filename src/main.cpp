// Entry point. Native: render frames to PPM. Emscripten: stream a small world
// (ground + character), upload it to the GPU worker, then drive a third-person
// camera + character movement each frame via SharedArrayBuffer.
#include "engine.h"
#include "linmath.h"
#include <cmath>
#include <cstdio>
#include <string>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#include <emscripten/html5.h>

static constexpr int MAX_OBJECTS = 8; // frame state holds up to 8 MVP matrices

// Uploads one object (mesh + texture) to the GPU worker.
EM_JS(void, js_gpu_object,
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
        type: 'object', positions: pos, uvs: uv, indices: idx,
        texW: texW, texH: texH, texData: texData,
    }, [pos.buffer, uv.buffer, idx.buffer, texData.buffer]);
});

// Signals the GPU worker that all objects have been uploaded.
EM_JS(void, js_gpu_done, (), {
    Module['gpuWorker'].postMessage({ type: 'done' });
});

// Writes `count` MVP matrices (column-major, 16 floats each) into the shared
// frame state and bumps the frame counter.
EM_JS(void, js_write_frames, (const float* mvps, int count, int frameNum), {
    if (!Module.__frameMVP)
        Module.__frameMVP = new Float32Array(Module['frameState'], 0, 8 * 16);
    if (!Module.__frameCounter)
        Module.__frameCounter = new Int32Array(Module['frameState'], 8 * 64, 1);
    Module.__frameMVP.set(HEAPF32.subarray(mvps >> 2, (mvps >> 2) + count * 16));
    Atomics.store(Module.__frameCounter, 0, frameNum);
});

// Reads the accumulated input deltas (and resets them) plus button/lock state.
EM_JS(void, js_read_input, (int* outDX, int* outDY, int* outWheel, int* outButtons, int* outLocked), {
    if (!Module.__inputI32)
        Module.__inputI32 = new Int32Array(Module['inputState'], 0, 8);
    var i = Module.__inputI32;
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

static engine::World g_world;
static float g_aspect = 640.0f / 480.0f;

// Third-person camera state (orbit around the character).
static float g_camYaw = 0.0f, g_camPitch = 0.35f, g_camDist = 6.0f;
// Character state (position on the ground plane; y stays 0).
static float g_charX = 0.0f, g_charZ = 0.0f;

static void em_main_loop() {
    static int frameNum = 0;
    frameNum++;

    int dx = 0, dy = 0, wheel = 0, buttons = 0, locked = 0;
    js_read_input(&dx, &dy, &wheel, &buttons, &locked);

    // Camera orbit (mouse drag) + zoom (wheel / Q / E).
    if (locked) {
        g_camYaw += (float)dx * 0.005f;
        g_camPitch += (float)dy * 0.005f;
    }
    if (js_key_down(81)) g_camDist += 0.05f;  // Q zoom out
    if (js_key_down(69)) g_camDist -= 0.05f;  // E zoom in
    g_camDist -= (float)wheel * 0.15f;

    if (g_camPitch > 1.4f) g_camPitch = 1.4f;
    if (g_camPitch < 0.05f) g_camPitch = 0.05f;
    if (g_camDist < 2.0f) g_camDist = 2.0f;
    if (g_camDist > 30.0f) g_camDist = 30.0f;

    // WASD moves the character relative to the camera's facing (XZ plane).
    float fx = -std::sin(g_camYaw), fz = -std::cos(g_camYaw); // forward
    float rx = std::cos(g_camYaw),  rz = -std::sin(g_camYaw); // right
    float mx = 0.0f, mz = 0.0f;
    if (js_key_down(87) || js_key_down(38)) { mx += fx; mz += fz; } // W / Up
    if (js_key_down(83) || js_key_down(40)) { mx -= fx; mz -= fz; } // S / Down
    if (js_key_down(65) || js_key_down(37)) { mx -= rx; mz -= rz; } // A / Left
    if (js_key_down(68) || js_key_down(39)) { mx += rx; mz += rz; } // D / Right
    float len = std::sqrt(mx * mx + mz * mz);
    if (len > 0.001f) {
        g_charX += mx / len * 0.08f;
        g_charZ += mz / len * 0.08f;
    }

    // Third-person follow camera: eye orbits the character, looking at it.
    float cp = std::cos(g_camPitch), sp = std::sin(g_camPitch);
    float cy = std::cos(g_camYaw),   sy = std::sin(g_camYaw);
    float ex = g_charX + g_camDist * cp * sy;
    float ey = 0.7f + g_camDist * sp;
    float ez = g_charZ + g_camDist * cp * cy;

    math::Mat4 proj = math::Mat4::perspective(60.0f, g_aspect, 0.1f, 200.0f);
    math::Mat4 view = math::Mat4::rotateX(-g_camPitch) * math::Mat4::rotateY(-g_camYaw)
                    * math::Mat4::translate(-ex, -ey, -ez);

    // Per-object model matrices: object 0 = ground (identity), object 1 = character.
    static math::Mat4 mvps[MAX_OBJECTS];
    int n = (int)g_world.objects.size();
    if (n > MAX_OBJECTS) n = MAX_OBJECTS;
    for (int i = 0; i < n; i++) {
        math::Mat4 model;
        model.identity();
        if (i == 1) model = math::Mat4::translate(g_charX, 0.5f, g_charZ);
        mvps[i] = proj * view * model;
    }

    js_write_frames(mvps[0].m, n, frameNum);
}

int main(int argc, char** argv) {
    engine::EngineConfig cfg;
    cfg.dataRoot = argc > 1 ? argv[1] : "/data"; // served from web/data/

    engine::Engine engine(cfg);
    if (!engine.init()) {
        std::fprintf(stderr, "init failed: %s\n", engine.error().c_str());
        return 1;
    }
    if (!engine.load_demo_world(g_world)) {
        std::fprintf(stderr, "world failed: %s\n", engine.error().c_str());
        return 1;
    }
    g_aspect = (float)engine.fbWidth() / (float)engine.fbHeight();

    for (const auto& obj : g_world.objects) {
        int vcount = (int)(obj.mesh.positions.size() / 3);
        int icount = (int)obj.mesh.indices.size();
        js_gpu_object(obj.mesh.positions.data(), obj.mesh.uvs.data(),
                      obj.mesh.indices.data(), vcount, icount,
                      obj.texture.rgba.data(), (int)obj.texture.width,
                      (int)obj.texture.height);
    }
    js_gpu_done();

    std::fprintf(stderr, "world uploaded (%zu objects); WASD to walk, drag to look\n",
                 g_world.objects.size());

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

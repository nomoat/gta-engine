#include "raster.h"
#include "linmath.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace engine {

namespace {

struct V2 { float x, y; };

float edge(const V2& a, const V2& b, const V2& p) {
    return (p.x - a.x) * (b.y - a.y) - (p.y - a.y) * (b.x - a.x);
}

} // namespace

void render_scene(const Scene& scene, float t, FrameBuffer& fb) {
    const uint32_t W = fb.width, H = fb.height;
    // Opaque clear (RGB 0x18, alpha 0xFF). Alpha must be 255 so the OffscreenCanvas
    // blit doesn't composite a half-transparent background over the page.
    uint8_t* cpx = fb.rgba.data();
    for (uint32_t i = 0; i < W * H; i++) {
        cpx[i * 4 + 0] = 0x18;
        cpx[i * 4 + 1] = 0x18;
        cpx[i * 4 + 2] = 0x18;
        cpx[i * 4 + 3] = 0xFF;
    }

    if (scene.mesh.positions.empty() || scene.mesh.indices.empty()) return;

    const float aspect = (float)W / (float)H;
    math::Mat4 proj = math::Mat4::perspective(60.0f, aspect, 0.1f, 100.0f);
    math::Mat4 view = math::Mat4::translate(0, 0, -3.0f);
    math::Mat4 model = math::Mat4::rotateY(t * 0.6f) * math::Mat4::rotateX(0.4f);
    math::Mat4 mvp = proj * view * model;

    const size_t vcount = scene.mesh.positions.size() / 3;

    // Transform vertices to clip space, then to screen space.
    std::vector<V2> screen(vcount);
    std::vector<float> invW(vcount);
    std::vector<float> zndc(vcount);
    for (size_t i = 0; i < vcount; i++) {
        math::Vec3 v{scene.mesh.positions[i * 3 + 0],
                     scene.mesh.positions[i * 3 + 1],
                     scene.mesh.positions[i * 3 + 2]};
        float w;
        math::Vec3 clip = math::transformPoint(mvp, v, w);
        float iw = w != 0 ? 1.0f / w : 1.0f;
        invW[i] = iw;
        zndc[i] = clip.z * iw;
        screen[i] = {(clip.x * iw * 0.5f + 0.5f) * W, (1.0f - (clip.y * iw * 0.5f + 0.5f)) * H};
    }

    std::vector<float> zbuf(W * H, 1e30f);

    const uint32_t tw = scene.texture.width ? scene.texture.width : 1;
    const uint32_t th = scene.texture.height ? scene.texture.height : 1;
    const uint8_t* tex = scene.texture.rgba.data();

    auto sample = [&](float u, float v) -> uint32_t {
        if (!tex) return 0xFFFFFFFF;
        u = u - std::floor(u);
        v = v - std::floor(v);
        uint32_t x = (uint32_t)(u * tw) % tw;
        uint32_t y = (uint32_t)(v * th) % th;
        const uint8_t* p = tex + (y * tw + x) * 4;
        return (p[0] << 0) | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
    };

    // Rasterize each triangle (indices are in triplets).
    for (size_t t0 = 0; t0 + 2 < scene.mesh.indices.size(); t0 += 3) {
        uint32_t ia = scene.mesh.indices[t0 + 0];
        uint32_t ib = scene.mesh.indices[t0 + 1];
        uint32_t ic = scene.mesh.indices[t0 + 2];
        if (ia >= vcount || ib >= vcount || ic >= vcount) continue;

        V2 a = screen[ia], b = screen[ib], c = screen[ic];
        float area = edge(a, b, c);
        if (area == 0) continue;
        if (area < 0) { std::swap(b, c); std::swap(ib, ic); area = -area; }

        float minx = std::max(0.0f, std::min({a.x, b.x, c.x}));
        float maxx = std::min((float)W, std::max({a.x, b.x, c.x}));
        float miny = std::max(0.0f, std::min({a.y, b.y, c.y}));
        float maxy = std::min((float)H, std::max({a.y, b.y, c.y}));

        for (int py = (int)miny; py < (int)maxy; py++) {
            for (int px = (int)minx; px < (int)maxx; px++) {
                V2 p{(float)px + 0.5f, (float)py + 0.5f};
                float w0 = edge(b, c, p);
                float w1 = edge(c, a, p);
                float w2 = edge(a, b, p);
                if (w0 < 0 || w1 < 0 || w2 < 0) continue;
                w0 /= area; w1 /= area; w2 /= area;

                float z = w0 * zndc[ia] + w1 * zndc[ib] + w2 * zndc[ic];
                size_t idx = (size_t)py * W + px;
                if (z >= zbuf[idx]) continue;
                zbuf[idx] = z;

                // Perspective-correct UV interpolation.
                float u = (w0 * scene.mesh.uvs[ia * 2 + 0] * invW[ia] +
                           w1 * scene.mesh.uvs[ib * 2 + 0] * invW[ib] +
                           w2 * scene.mesh.uvs[ic * 2 + 0] * invW[ic]) /
                          (w0 * invW[ia] + w1 * invW[ib] + w2 * invW[ic]);
                float v = (w0 * scene.mesh.uvs[ia * 2 + 1] * invW[ia] +
                           w1 * scene.mesh.uvs[ib * 2 + 1] * invW[ib] +
                           w2 * scene.mesh.uvs[ic * 2 + 1] * invW[ic]) /
                          (w0 * invW[ia] + w1 * invW[ib] + w2 * invW[ic]);

                uint32_t rgba = sample(u, v);
                uint8_t* dst = fb.rgba.data() + idx * 4;
                dst[0] = (uint8_t)(rgba >> 0);
                dst[1] = (uint8_t)(rgba >> 8);
                dst[2] = (uint8_t)(rgba >> 16);
                dst[3] = 0xFF;
            }
        }
    }
}

bool write_ppm(const FrameBuffer& fb, const char* path) {
    FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    std::fprintf(f, "P6\n%u %u\n255\n", fb.width, fb.height);
    // PPM needs RGB triplets.
    std::vector<uint8_t> row(fb.width * 3);
    for (uint32_t y = 0; y < fb.height; y++) {
        for (uint32_t x = 0; x < fb.width; x++) {
            const uint8_t* p = fb.rgba.data() + (size_t)(y * fb.width + x) * 4;
            row[x * 3 + 0] = p[0];
            row[x * 3 + 1] = p[1];
            row[x * 3 + 2] = p[2];
        }
        std::fwrite(row.data(), 1, row.size(), f);
    }
    std::fclose(f);
    return true;
}

} // namespace engine

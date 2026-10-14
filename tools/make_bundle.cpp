// Packs individual asset files into a bundle (the engine's .rpf analogue).
// Usage: make_bundle <out.bundle> <type> <name> <file> [<type> <name> <file> ...]
//   type: mesh | texture | other
//   name: the entry name inside the bundle (e.g. "quad.mesh", "checker.tex")
#include "../src/bundle.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace engine;

static bool read_file(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    std::streamoff n = f.tellg();
    f.seekg(0, std::ios::beg);
    out.resize((size_t)n);
    f.read(reinterpret_cast<char*>(out.data()), n);
    return f.good() || f.eof();
}

static AssetType parse_type(const char* s) {
    if (std::strcmp(s, "mesh") == 0) return AssetType::Mesh;
    if (std::strcmp(s, "texture") == 0) return AssetType::Texture;
    return AssetType::Other;
}

int main(int argc, char** argv) {
    if (argc < 5 || (argc - 2) % 3 != 0) {
        std::fprintf(stderr, "usage: make_bundle <out.bundle> <type> <name> <file> [...]\n");
        return 1;
    }

    std::vector<BundleEntry> entries;
    std::vector<std::vector<uint8_t>> payloads;
    int n = (argc - 2) / 3;

    for (int i = 0; i < n; i++) {
        BundleEntry e;
        e.type = parse_type(argv[2 + i * 3 + 0]);
        e.name = argv[2 + i * 3 + 1];
        payloads.emplace_back();
        if (!read_file(argv[2 + i * 3 + 2], payloads.back())) {
            std::fprintf(stderr, "cannot read %s\n", argv[2 + i * 3 + 2]);
            return 1;
        }
        e.length = payloads.back().size();
        entries.push_back(std::move(e));
    }

    // Lay out header then payloads.
    std::vector<uint8_t> header = build_bundle_header(entries);
    uint64_t cursor = header.size();
    for (int i = 0; i < n; i++) {
        entries[i].offset = cursor;
        cursor += payloads[i].size();
    }
    header = build_bundle_header(entries); // rebuild with real offsets

    std::ofstream out(argv[1], std::ios::binary);
    if (!out) { std::fprintf(stderr, "cannot write %s\n", argv[1]); return 1; }
    out.write(reinterpret_cast<const char*>(header.data()), header.size());
    for (auto& p : payloads) out.write(reinterpret_cast<const char*>(p.data()), p.size());
    std::printf("wrote %s (%llu bytes, %d entries)\n", argv[1], (unsigned long long)cursor, n);
    return 0;
}

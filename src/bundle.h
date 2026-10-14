// Bundle container: the engine's analogue of a GTA .rpf packfile. A bundle holds
// several assets (mesh, texture, ...) laid out contiguously, with a header table
// at the front. The manifest lists which byte ranges of the bundle to prefetch;
// everything else is streamed on demand via IStreamStore::read().
//
// On-disk layout (little-endian):
//   magic  "BNDL1"        (5 bytes)
//   u32    entryCount
//   entryCount x:
//     u16    nameLen, name bytes
//     u8     type          (1 = mesh, 2 = texture, 0 = other)
//     u64    offset
//     u64    length
//   ... raw asset bytes (each entry starts at its offset) ...
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace engine {

enum class AssetType : uint8_t { Other = 0, Mesh = 1, Texture = 2 };

struct BundleEntry {
    std::string name;
    AssetType type = AssetType::Other;
    uint64_t offset = 0;
    uint64_t length = 0;
};

struct Bundle {
    std::vector<BundleEntry> entries;
    const BundleEntry* find(const std::string& name) const;
};

// Parses a bundle header (the first bytes of the file). Returns false on error.
bool parse_bundle_header(const uint8_t* data, size_t size, Bundle& out);

// Serializes a bundle header (used by tools/make_bundle). Returns empty on error.
std::vector<uint8_t> build_bundle_header(const std::vector<BundleEntry>& entries);

} // namespace engine

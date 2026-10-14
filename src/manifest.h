// Boot set manifest: mirrors the GTA port's data/bootset.json format
//   [ [ "path", [ [offset, length], [offset, length], ... ] ], ... ]
// The engine prefetches the listed byte ranges at startup (they are the pieces of
// each archive that are needed first), then streams everything else on demand.
#pragma once
#include <string>
#include <vector>
#include "stream.h"

namespace engine {

struct FilePrefetch {
    std::string path;
    std::vector<Range> ranges;
};

struct Manifest {
    std::vector<FilePrefetch> files;
};

// Parses a bootset.json document. Returns false and leaves `out` untouched on error.
bool parse_manifest(const std::string& json_text, Manifest& out);

} // namespace engine

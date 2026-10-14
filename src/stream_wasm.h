// WebAssembly stream store: the same IStreamStore interface, backed by HTTP
// Range requests (synchronous fetch, only allowed on a worker — which is exactly
// where this engine runs: web/loader.js). Mirrors the GTA port's io_worker.js +
// bootset.json behaviour: never fetch a whole archive, only [offset, length).
//
// Only compiled under __EMSCRIPTEN__ (see CMakeLists.txt).
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "stream.h"

namespace engine {

class EmscriptenStreamStore : public IStreamStore {
public:
    explicit EmscriptenStreamStore(std::string root = "/data");
    bool read(const std::string& path, uint64_t offset, uint64_t length,
              std::vector<uint8_t>& out) override;
    bool readAll(const std::string& path, std::vector<uint8_t>& out) override;
    uint64_t size(const std::string& path) override;
private:
    std::string root_; // URL prefix, e.g. "/data"
};

} // namespace engine

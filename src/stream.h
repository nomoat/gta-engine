// Streaming IO store: the engine never loads a whole file at once. It asks for a
// byte range [offset, offset+length) of a named file — exactly how the GTA port's
// io_worker + bootset.json stream big .rpf archives over HTTP Range requests.
//
// Native builds use the local filesystem; the WebAssembly build swaps this for the
// io_worker.js transport (HTTP Range) with the same interface.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace engine {

struct Range { uint64_t offset = 0; uint64_t length = 0; };

class IStreamStore {
public:
    virtual ~IStreamStore() = default;
    // Reads exactly [offset, offset+length) into out. Returns false on any error
    // (short read, missing file, network failure...).
    virtual bool read(const std::string& path, uint64_t offset, uint64_t length,
                      std::vector<uint8_t>& out) = 0;
    // Reads a whole (small) file — used for the boot manifest. Returns false on error.
    virtual bool readAll(const std::string& path, std::vector<uint8_t>& out) = 0;
    // Total size of a file, or 0 if unknown/unavailable.
    virtual uint64_t size(const std::string& path) = 0;
};

// Filesystem-backed store rooted at `root` ("" means current directory).
class FileStreamStore : public IStreamStore {
public:
    explicit FileStreamStore(std::string root = "");
    bool read(const std::string& path, uint64_t offset, uint64_t length,
              std::vector<uint8_t>& out) override;
    bool readAll(const std::string& path, std::vector<uint8_t>& out) override;
    uint64_t size(const std::string& path) override;
private:
    std::string root_;
};

} // namespace engine

#include "stream.h"
#include <fstream>

namespace engine {

FileStreamStore::FileStreamStore(std::string root) : root_(std::move(root)) {
    if (!root_.empty() && root_.back() != '/') root_ += '/';
}

bool FileStreamStore::read(const std::string& path, uint64_t offset, uint64_t length,
                           std::vector<uint8_t>& out) {
    std::ifstream f(root_ + path, std::ios::binary);
    if (!f) return false;
    f.seekg((std::streamoff)offset, std::ios::beg);
    out.resize(length);
    f.read(reinterpret_cast<char*>(out.data()), (std::streamsize)length);
    return f.good() || f.eof(); // tolerate exact-EOF reads
}

uint64_t FileStreamStore::size(const std::string& path) {
    std::ifstream f(root_ + path, std::ios::binary | std::ios::ate);
    if (!f) return 0;
    return (uint64_t)f.tellg();
}

bool FileStreamStore::readAll(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(root_ + path, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    std::streamoff n = f.tellg();
    f.seekg(0, std::ios::beg);
    out.resize((size_t)n);
    f.read(reinterpret_cast<char*>(out.data()), n);
    return f.good() || f.eof();
}

} // namespace engine

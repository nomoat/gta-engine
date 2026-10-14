// WebAssembly stream store: the same IStreamStore interface, backed by HTTP
// Range requests issued from this (worker) thread. Uses a synchronous XHR here —
// allowed on a worker and proven by the Emscripten glue itself, which loads
// game.wasm the same way. (A production build can swap this for the async
// io_worker.js + SharedArrayBuffer bridge without changing the interface; the
// streaming model — manifest + byte-range reads — stays identical.)
//
// Only compiled under __EMSCRIPTEN__ (see CMakeLists.txt).
#include "stream_wasm.h"
#include <cstring>
#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

namespace engine {

EmscriptenStreamStore::EmscriptenStreamStore(std::string root) : root_(std::move(root)) {
    if (!root_.empty() && root_.back() == '/') root_.pop_back();
}

#ifdef __EMSCRIPTEN__

// Fetches [offset, offset+length) of `url` (length==0 -> whole file) and writes
// the bytes directly into the wasm heap at `dst`. Returns bytes written or 0.
EM_JS(int, js_fetch_into, (const char* url, double offset, double length, uint8_t* dst), {
    try {
        var p = url, end = url;
        while (HEAPU8[end]) end++;
        var u = new TextDecoder('utf-8').decode(HEAPU8.subarray(p, end));

        var xhr = new XMLHttpRequest();
        xhr.open('GET', u, false); // synchronous; only legal on a worker
        if (length > 0)
            xhr.setRequestHeader('Range', 'bytes=' + offset + '-' + (offset + length - 1));
        xhr.responseType = 'arraybuffer';
        xhr.send(null);

        if (xhr.status !== 200 && xhr.status !== 206) return 0;

        var buf = new Uint8Array(xhr.response);
        var data;
        if (xhr.status === 206) data = buf;
        else if (length > 0) data = buf.subarray(offset, offset + length); // server ignored Range
        else data = buf;

        HEAPU8.set(data, dst);
        return data.length;
    } catch (e) {
        try { postMessage({ log: '[fetch] ' + (e && e.message ? e.message : e) }); } catch (x) {}
        return 0;
    }
});

// Whole-file size via a full GET (used only for small manifests/configs).
EM_JS(double, js_fetch_size, (const char* url), {
    try {
        var p = url, end = url;
        while (HEAPU8[end]) end++;
        var u = new TextDecoder('utf-8').decode(HEAPU8.subarray(p, end));
        var xhr = new XMLHttpRequest();
        xhr.open('GET', u, false);
        xhr.responseType = 'arraybuffer';
        xhr.send(null);
        if (xhr.status !== 200) return 0;
        return xhr.response.byteLength;
    } catch (e) {
        return 0;
    }
});

bool EmscriptenStreamStore::read(const std::string& path, uint64_t offset, uint64_t length,
                                 std::vector<uint8_t>& out) {
    if (length == 0) { out.clear(); return true; }
    std::string url = root_ + "/" + path;

    out.resize(length);
    int n = js_fetch_into(url.c_str(), (double)offset, (double)length, out.data());
    if (n < 0 || (uint64_t)n != length) { out.clear(); return false; }
    return true;
}

bool EmscriptenStreamStore::readAll(const std::string& path, std::vector<uint8_t>& out) {
    std::string url = root_ + "/" + path;
    double sz = js_fetch_size(url.c_str());
    if (sz <= 0) return false;
    return read(path, 0, (uint64_t)sz, out);
}

uint64_t EmscriptenStreamStore::size(const std::string& path) {
    std::string url = root_ + "/" + path;
    double sz = js_fetch_size(url.c_str());
    return sz > 0 ? (uint64_t)sz : 0;
}

#else  // __EMSCRIPTEN__

bool EmscriptenStreamStore::read(const std::string&, uint64_t, uint64_t, std::vector<uint8_t>&) { return false; }
bool EmscriptenStreamStore::readAll(const std::string&, std::vector<uint8_t>&) { return false; }
uint64_t EmscriptenStreamStore::size(const std::string&) { return 0; }

#endif // __EMSCRIPTEN__

} // namespace engine

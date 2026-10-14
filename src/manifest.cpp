#include "manifest.h"
#include "json.h"

namespace engine {

bool parse_manifest(const std::string& json_text, Manifest& out) {
    using namespace js;
    Value root;
    try {
        root = parse(json_text);
    } catch (...) {
        return false;
    }
    if (!root.isArray()) return false;

    Manifest m;
    for (const Value& entry : root.asArray()) {
        if (!entry.isArray() || entry.asArray().size() < 1) return false;
        const Array& e = entry.asArray();
        if (!e[0].isString()) return false;

        FilePrefetch fp;
        fp.path = e[0].asString();

        // Ranges are optional (a file may be listed with no prefetch).
        if (e.size() >= 2 && e[1].isArray()) {
            for (const Value& r : e[1].asArray()) {
                if (!r.isArray() || r.asArray().size() < 2) return false;
                const Array& rr = r.asArray();
                if (!rr[0].isNumber() || !rr[1].isNumber()) return false;
                Range range;
                range.offset = (uint64_t)rr[0].asInt();
                range.length = (uint64_t)rr[1].asInt();
                fp.ranges.push_back(range);
            }
        }
        m.files.push_back(std::move(fp));
    }
    out = std::move(m);
    return true;
}

} // namespace engine

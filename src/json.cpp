#include "json.h"
#include <cctype>
#include <cmath>
#include <cstring>

namespace js {

namespace {

struct Parser {
    const char* p;
    const char* end;

    explicit Parser(const std::string& s) : p(s.data()), end(s.data() + s.size()) {}

    void ws() {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p;
    }
    [[noreturn]] void fail(const char* msg) {
        throw std::runtime_error(std::string("json: ") + msg);
    }
    char peek() { ws(); if (p >= end) fail("unexpected end"); return *p; }
    bool consume(char c) { ws(); if (p < end && *p == c) { ++p; return true; } return false; }
    void expect(char c) { if (!consume(c)) fail("expected token"); }

    Value value() {
        ws();
        char c = peek();
        if (c == '"') return string();
        if (c == '[') return array();
        if (c == '{') return object();
        if (c == 't') { lit("true"); return Value(true); }
        if (c == 'f') { lit("false"); return Value(false); }
        if (c == 'n') { lit("null"); return Value(nullptr); }
        return number();
    }

    void lit(const char* s) {
        size_t n = strlen(s);
        if ((size_t)(end - p) < n || strncmp(p, s, n) != 0) fail("bad literal");
        p += n;
    }

    Value string() {
        expect('"');
        std::string out;
        while (p < end && *p != '"') {
            char c = *p++;
            if (c == '\\') {
                if (p >= end) fail("bad escape");
                char e = *p++;
                switch (e) {
                    case '"': out += '"'; break;
                    case '\\': out += '\\'; break;
                    case '/': out += '/'; break;
                    case 'n': out += '\n'; break;
                    case 't': out += '\t'; break;
                    case 'r': out += '\r'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'u': {
                        if (end - p < 4) fail("bad \\u");
                        unsigned cp = 0;
                        for (int i = 0; i < 4; i++) {
                            char h = *p++;
                            cp <<= 4;
                            if (h >= '0' && h <= '9') cp |= h - '0';
                            else if (h >= 'a' && h <= 'f') cp |= h - 'a' + 10;
                            else if (h >= 'A' && h <= 'F') cp |= h - 'A' + 10;
                            else fail("bad hex");
                        }
                        // Encode as UTF-8 (handles BMP; surrogate pairs folded roughly).
                        if (cp < 0x80) out += (char)cp;
                        else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
                        else { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
                        break;
                    }
                    default: fail("bad escape");
                }
            } else {
                out += c;
            }
        }
        expect('"');
        return Value(out);
    }

    Value number() {
        const char* start = p;
        if (p < end && (*p == '-' || *p == '+')) ++p;
        while (p < end && (isdigit((unsigned char)*p) || *p == '.' || *p == 'e' || *p == 'E' || *p == '-' || *p == '+')) ++p;
        std::string s(start, p);
        if (s.empty()) fail("bad number");
        char* endp = nullptr;
        double d = strtod(s.c_str(), &endp);
        if (endp != s.c_str() + s.size()) fail("bad number");
        return Value(d);
    }

    Value array() {
        expect('[');
        Array a;
        if (consume(']')) return Value(std::move(a));
        for (;;) {
            a.push_back(value());
            if (consume(']')) break;
            expect(',');
        }
        return Value(std::move(a));
    }

    Value object() {
        expect('{');
        Object o;
        if (consume('}')) return Value(std::move(o));
        for (;;) {
            std::string k = string().asString();
            expect(':');
            o[k] = value();
            if (consume('}')) break;
            expect(',');
        }
        return Value(std::move(o));
    }
};

} // namespace

Value parse(const std::string& text) {
    Parser p(text);
    Value v = p.value();
    p.ws();
    if (p.p != p.end) throw std::runtime_error("json: trailing data");
    return v;
}

} // namespace js

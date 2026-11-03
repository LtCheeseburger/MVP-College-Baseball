#pragma once
#include <cctype>
#include <cstdlib>
#include <string>
#include <vector>
#include <utility>
namespace patch_json {
struct Json {
    enum Type { Null, Bool, Number, String, Array, Object } type = Null;
    bool b = false;
    double num = 0;
    std::string s;
    std::vector<Json> items;
    std::vector<std::pair<std::string, Json>> fields;

    const Json* Get(const char* key) const {
        for (auto& [k, v] : fields)
            if (k == key) return &v;
        return nullptr;
    }
    std::string Str(const char* key, const std::string& fallback = {}) const {
        const Json* v = Get(key);
        return v && v->type == String ? v->s : fallback;
    }
    double Num(const char* key, double fallback) const {
        const Json* v = Get(key);
        return v && v->type == Number ? v->num : fallback;
    }
    bool Flag(const char* key) const {
        const Json* v = Get(key);
        return v && v->type == Bool && v->b;
    }
    std::vector<std::string> Strings(const char* key) const {
        std::vector<std::string> out;
        if (const Json* v = Get(key); v && v->type == Array)
            for (const Json& i : v->items)
                if (i.type == String) out.push_back(i.s);
        return out;
    }
};

struct Parser {
    const std::string& t;
    size_t p = 0;
    bool bad = false;

    void Space() {
        while (p < t.size() && std::isspace(uint8_t(t[p]))) p++;
    }
    bool Eat(char c) {
        Space();
        if (p < t.size() && t[p] == c) {
            p++;
            return true;
        }
        return false;
    }
    std::string Text() {
        std::string out;
        if (!Eat('"')) {
            bad = true;
            return out;
        }
        while (p < t.size() && t[p] != '"') {
            char c = t[p++];
            if (c != '\\') {
                out += c;
                continue;
            }
            if (p >= t.size()) break;
            c = t[p++];
            switch (c) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'u': {
                    unsigned v = p + 4 <= t.size() ? unsigned(std::strtoul(t.substr(p, 4).c_str(), nullptr, 16)) : 0;
                    p += 4;
                    if (v < 0x80) {
                        out += char(v);
                    } else if (v < 0x800) {
                        out += char(0xC0 | v >> 6);
                        out += char(0x80 | (v & 0x3F));
                    } else {
                        out += char(0xE0 | v >> 12);
                        out += char(0x80 | (v >> 6 & 0x3F));
                        out += char(0x80 | (v & 0x3F));
                    }
                    break;
                }
                default: out += c;
            }
        }
        if (p >= t.size()) bad = true;
        p++;
        return out;
    }
    Json Value(int depth = 0) {
        Json v;
        Space();
        if (p >= t.size() || depth > 32) {
            bad = true;
            return v;
        }
        char c = t[p];
        if (c == '{') {
            p++;
            v.type = Json::Object;
            if (Eat('}')) return v;
            do {
                std::string k = Text();
                if (!Eat(':')) bad = true;
                v.fields.push_back({k, Value(depth + 1)});
            } while (!bad && Eat(','));
            if (!Eat('}')) bad = true;
        } else if (c == '[') {
            p++;
            v.type = Json::Array;
            if (Eat(']')) return v;
            do v.items.push_back(Value(depth + 1));
            while (!bad && Eat(','));
            if (!Eat(']')) bad = true;
        } else if (c == '"') {
            v.type = Json::String;
            v.s = Text();
        } else if (t.compare(p, 4, "true") == 0 || t.compare(p, 5, "false") == 0) {
            v.type = Json::Bool;
            v.b = t[p] == 't';
            p += v.b ? 4 : 5;
        } else if (t.compare(p, 4, "null") == 0) {
            p += 4;
        } else {
            char* end = nullptr;
            v.type = Json::Number;
            v.num = std::strtod(t.c_str() + p, &end);
            if (end == t.c_str() + p) bad = true;
            p = size_t(end - t.c_str());
        }
        return v;
    }
};

inline bool ParseJson(const std::string& text, Json& out) {
    Parser ps{text};
    out = ps.Value();
    ps.Space();
    return !ps.bad && ps.p == text.size() && out.type == Json::Object;
}


}

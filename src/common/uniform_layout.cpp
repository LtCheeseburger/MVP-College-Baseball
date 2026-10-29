#include "uniforms.h"

#include <charconv>
#include <sstream>

namespace game::uniforms {
namespace {
std::vector<std::string> Fields(const std::string& line) {
    std::vector<std::string> fields;
    size_t p = 0;
    do {
        size_t end = line.find(',', p);
        fields.push_back(line.substr(p, end - p));
        if (end == std::string::npos) break;
        p = end + 1;
    } while (true);
    return fields;
}
std::string Join(const std::vector<std::string>& fields) {
    std::string line;
    for (const auto& field : fields) { if (!line.empty()) line += ','; line += field; }
    return line;
}
bool Number(const std::string& s, int& value) {
    auto [end, error] = std::from_chars(s.data(), s.data() + s.size(), value);
    return error == std::errc{} && end == s.data() + s.size();
}
size_t Indent(const std::string& s) { return s.find_first_not_of(' '); }
}  // namespace

bool PatchLayout(std::vector<uint8_t>& bytes) {
    std::string text(bytes.begin(), bytes.end());
    if (text.find("TB:UNIFORMHAT,") != std::string::npos) return false;
    const std::string eol = text.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    std::vector<std::string> lines;
    std::istringstream stream(text);
    for (std::string line; std::getline(stream, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(std::move(line));
    }
    unsigned patched = 0;
    for (size_t first = 0; first < lines.size(); ++first) {
        if (lines[first].find("GR:HOMEJERSEY,") == std::string::npos &&
            lines[first].find("GR:AWAYJERSEY,") == std::string::npos) continue;
        const size_t indent = Indent(lines[first]);
        size_t end = first + 1;
        while (end < lines.size() && (lines[end].empty() || Indent(lines[end]) > indent)) ++end;
        size_t label = first;
        for (size_t i = first + 1; i < end; ++i)
            if (lines[i].find("TB:JERSEYNAME,") != std::string::npos) label = i;
        if (label == first) return false;
        auto original = Fields(lines[label]);
        int y = 0;
        if (original.size() < 40 || !Number(original[4], y)) return false;
        // Keep the artwork and animated group intact. The existing right-hand choice panel grows vertically;
        // four native text boxes replace its single line. No exported game asset is stored in the repository.
        for (size_t i = first + 1; i < end; ++i) {
            auto f = Fields(lines[i]);
            if (f.size() < 7) continue;
            const auto type = f[0].substr(Indent(f[0]));
            if (type == "BU:NEXT" || type == "BU:PREV") f[1] = "0";
            if (type == "FC:FC1" || type.starts_with("FR:") ||
                type == "SH:F4" || type == "SH:F5" || type == "SH:F6") {
                f[4] = std::to_string(y - 52);
                f[6] = "118";
            }
            if (type == "TX:JERSEYTITLE") f[4] = std::to_string(y + 72);
            lines[i] = Join(f);
        }
        std::vector<std::string> rows;
        constexpr const char* names[] = {"JERSEYNAME", "UNIFORMHAT", "UNIFORMJERSEY", "UNIFORMPANTS", "UNIFORMHELP"};
        for (unsigned row = 0; row < 5; ++row) {
            auto f = original;
            f[0] = std::string(Indent(original[0]), ' ') + "TB:" + names[row];
            f[3] = "364";
            f[4] = std::to_string(y - 40 + int(row) * 20);
            f[5] = "256";
            f[6] = "18";
            f[13] = row == 4 ? "twc14_en.ffn" : "twc16_en.ffn";
            // Leave alignment and native text-state colors as authored.
            rows.push_back(Join(f));
        }
        lines.erase(lines.begin() + std::ptrdiff_t(label));
        lines.insert(lines.begin() + std::ptrdiff_t(label), rows.begin(), rows.end());
        first = end + 3;
        ++patched;
    }
    if (patched != 2) return false;  // another/custom screen layout: leave it completely unchanged
    std::string out;
    for (size_t i = 0; i < lines.size(); ++i) out += lines[i] + (i + 1 < lines.size() ? eol : "");
    if (text.ends_with(eol)) out += eol;
    bytes.assign(out.begin(), out.end());
    return true;
}
}  // namespace game::uniforms

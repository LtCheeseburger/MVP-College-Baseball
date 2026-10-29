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
        // Bitmap frames keep their native texture size even when their FEL bounds grow. A native rectangle
        // behind the rows supplies the larger panel; the hook hides the old frame/arrows after activation.
        const std::string pad(Indent(original[0]), ' ');
        std::vector<std::string> rows = {pad + "RT:UNIFORMBACK,1,0,358," + std::to_string(y - 46) +
            ",274,106,1,1,1,1,1,012,020,044,0,1,1,000,000,000,0,0,220,-1,0,0,0"};
        constexpr const char* names[] = {"JERSEYNAME", "UNIFORMHAT", "UNIFORMJERSEY", "UNIFORMPANTS", "UNIFORMHELP"};
        for (unsigned row = 0; row < 5; ++row) {
            auto f = original;
            f[0] = std::string(Indent(original[0]), ' ') + "TB:" + names[row];
            f[3] = "364";
            f[4] = std::to_string(y - 40 + int(row) * 20);
            f[5] = "256";
            f[6] = "18";
            f[13] = row == 4 ? "twc14_en.ffn" : "twc16_en.ffn";
            // Native unselected/disabled state colors were dark gray against a one-line highlight. All rows
            // must stay readable on the expanded panel; the cursor and selected state still identify focus.
            for (unsigned color = 16; color <= 30; ++color) f[color] = "235";
            rows.push_back(Join(f));
        }
        lines.erase(lines.begin() + std::ptrdiff_t(label));
        lines.insert(lines.begin() + std::ptrdiff_t(label), rows.begin(), rows.end());
        first = end + 4;
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

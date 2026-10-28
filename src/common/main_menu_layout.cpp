// The main menu's Quit Game row. The Xbox main menu (fes_mainmenu.fel in frontend.big, group CONSOLEMENU) lists its
// rows in a 1 x 7 grid named CELL; the list code takes its row count and up/down movement from that grid, and the
// MainMenu screen (vtable 0x643630) opens the game's yes/no popup for a row whose text box is named QUIT (0x517F60),
// a leftover of the PC version. The layout is patched as the archive is opened, nothing on disk changes: the grid
// gets an eighth row, X0Y7, copied from the row above it (X0Y6, "Inside EA") one row step lower. The popup's answer
// is handled in src/game/main_menu.cpp.
#include <cstdlib>
#include <string>
#include <vector>

#include "game_hooks.h"
#include "kernel/modvfs.h"

namespace {

constexpr int kRowStep = 29;  // the grid's rows are 29 apart

std::string Field(const std::string& line, size_t i) {
    size_t a = 0;
    for (size_t k = 0; k < i && a != std::string::npos; k++) a = line.find(',', a + 1);
    if (a == std::string::npos) return {};
    size_t b = line.find(',', a + 1);
    return line.substr(a + 1, b == std::string::npos ? std::string::npos : b - a - 1);
}

// a record "TYPE:NAME,visible,?,x,y,w,h,..." with its y moved down
std::string MoveDown(const std::string& line, int dy) {
    size_t a = 0;
    for (int k = 0; k < 4 && a != std::string::npos; k++) a = line.find(',', a + 1);
    size_t b = a == std::string::npos ? a : line.find(',', a + 1);
    if (b == std::string::npos) return line;
    return line.substr(0, a + 1) + std::to_string(std::atoi(line.c_str() + a + 1) + dy) + line.substr(b);
}

size_t Indent(const std::string& line) {
    return line.find_first_not_of(' ');
}

bool AddQuitRow(std::vector<uint8_t>& data) {
    std::string text(data.begin(), data.end());
    const std::string eol = text.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    std::vector<std::string> lines;
    for (size_t p = 0; p < text.size();) {
        size_t e = text.find(eol, p);
        if (e == std::string::npos) e = text.size();
        lines.push_back(text.substr(p, e - p));
        p = e + eol.size();
    }
    // CONSOLEMENU's grid, then its first row (X0Y6, rows are listed bottom up)
    size_t i = 0;
    while (i < lines.size() && lines[i].find("GR:CONSOLEMENU,") == std::string::npos) i++;
    while (i < lines.size() && lines[i].find("GG:CELL,") == std::string::npos) i++;
    if (i == lines.size() || Field(lines[i], 17) != "7" || Field(lines[i], 19) != "7") return false;
    size_t grid = i, row = i + 1;
    if (row == lines.size() || lines[row].find("GR:X0Y6,") == std::string::npos) return false;
    size_t row_indent = Indent(lines[row]);
    size_t end = row + 1;
    while (end < lines.size() && Indent(lines[end]) > row_indent) end++;

    // 1 x 8, one row step taller
    std::string g = lines[grid];
    auto set = [&](size_t f, const std::string& v) {
        size_t a = 0;
        for (size_t k = 0; k < f; k++) a = g.find(',', a + 1);
        size_t b = g.find(',', a + 1);
        g = g.substr(0, a + 1) + v + g.substr(b);
    };
    set(6, std::to_string(std::atoi(Field(g, 6).c_str()) + kRowStep));
    set(17, "8");
    set(19, "8");

    // the copy: the row's highlight frame and text box; its button with the side icon (INSIDEEABUT) stays out
    std::vector<std::string> quit;
    for (size_t k = row; k < end; k++) {
        const std::string& l = lines[k];
        size_t ind = Indent(l);
        std::string record = l.substr(ind, l.find(',') - ind);
        if (record == "BU:INSIDEEABUT") {
            while (k + 1 < end && Indent(lines[k + 1]) > ind) k++;
            continue;
        }
        std::string name = record.substr(3);
        std::string to = name == "X0Y6" ? "X0Y7" : name == "INSIDEBACK" ? "QUITBACK" : name == "INSIDEEA1" ? "QUIT1"
                         : name == "INSIDEEA" ? "QUIT" : name;
        std::string out = l.substr(0, ind + 3) + to + l.substr(l.find(','));
        out = MoveDown(out, kRowStep);
        if (record == "TB:INSIDEEA") {
            // the text box's string id follows its font
            size_t font = out.find(".ffn,");
            size_t b = font == std::string::npos ? font : out.find(',', font + 5);
            if (b == std::string::npos) return false;
            out = out.substr(0, font + 5) + std::to_string(game::kQuitString) + out.substr(b);
        }
        quit.push_back(out);
    }
    lines[grid] = g;
    lines.insert(lines.begin() + std::ptrdiff_t(row), quit.begin(), quit.end());

    std::string joined;
    for (size_t k = 0; k < lines.size(); k++) joined += lines[k] + (k + 1 < lines.size() ? eol : "");
    if (text.size() >= eol.size() && text.compare(text.size() - eol.size(), eol.size(), eol) == 0) joined += eol;
    data.assign(joined.begin(), joined.end());
    return true;
}

const bool g_registered = [] {
    kernel::modvfs::AddPatch("data/frontend/frontend.big", "fes_mainmenu.fel", AddQuitRow);
    return true;
}();

}  // namespace

#include "common/uniforms.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <set>

using namespace game::uniforms;
#define CHECK(condition) do { if (!(condition)) { std::cerr << __LINE__ << ": " #condition "\n"; std::exit(1); } } while (0)

std::string Manifest(std::string extra = {}) {
    return "format=1\nid='navy'\nname='Navy'\nteam=44\npart='hat'\ndonor='a'\ntexture='navy.xsh'\n" + extra;
}
std::string Replace(std::string text, const std::string& from, const std::string& to) {
    text.replace(text.find(from), from.size(), to);
    return text;
}
void Catalog() {
    std::string error;
    auto hat = ParsePiece(Manifest(), error);
    CHECK(hat && error.empty() && hat->team == 44 && hat->part == Part::Hat && hat->variant == 0);
    const std::pair<std::string, std::string> invalid[] = {{"format=1", "format=2"}, {"team=44", "team=-1"},
        {"team=44", "team=256"}, {"team=44", "team='44'"}, {"'navy.xsh'", "'../navy.xsh'"},
        {"'navy.xsh'", "'a\\navy.xsh'"}, {"'navy.xsh'", "'hat.exe'"}, {"part='hat'", "part='helmet'"},
        {"donor='a'", "donor='e'"}, {"donor='a'", "donor='aa'"}, {"id='navy'", "id='stock-0'"},
        {"name='Navy'", "name=''"}, {"name='Navy'", "name='1234567890123456789012345'"}};
    for (const auto& [from, to] : invalid) { CHECK(!ParsePiece(Replace(Manifest(), from, to), error)); CHECK(!error.empty()); }
    CHECK(!ParsePiece(Manifest("font='letters.xsh'\n"), error));
    CHECK(!ParsePiece(Manifest("texture='duplicate.xsh'\n"), error));
    auto jersey = ParsePiece(Replace(Manifest("font='letters.xsh'\n"), "part='hat'", "part='jersey'"), error);
    CHECK(jersey && jersey->font == "letters.xsh");
    CHECK(ParsePiece(Replace(Manifest(), "donor='a'", "donor='p'"), error)->variant == 13);
}
void SelectionBehavior() {
    const Preset presets[] = {{0, "Home"}, {1, "Away"}, {2, "Alternate"}, {1, "Duplicate"}, {14, "Invalid"}};
    const Piece added[] = {{"navy", "Navy", Part::Hat, 44, 0, "navy.xsh", {}},
        {"navy", "Duplicate", Part::Hat, 44, 0, "duplicate.xsh", {}},
        {"other", "Other team", Part::Pants, 45, 0, "other.xsh", {}},
        {"missing", "Missing donor", Part::Pants, 44, 3, "missing.xsh", {}}};
    const auto wardrobe = MakeWardrobe(44, presets, added);
    CHECK(wardrobe.presets.size() == 3 && wardrobe.pieces[0].size() == 4 && wardrobe.pieces[2].size() == 3);
    Selection home(44, wardrobe, 0), away(44, wardrobe, 1);
    CHECK(!home.IsCustom() && home.Current().pieces[0].variant == 0);
    CHECK(!away.IsCustom() && away.Current().pieces[2].variant == 1);
    home.MoveRow(1); home.Cycle(-1);  // standalone hat, wraps backward
    CHECK(home.Current().pieces[0].id == "navy" && home.Current().pieces[1].variant == 0);
    home.MoveRow(1); home.Cycle(1);   // away jersey, leaves hat and pants
    CHECK(home.Current().pieces[1].variant == 1 && home.Current().pieces[2].variant == 0);
    CHECK(home.IsCustom() && home.Labels()[0] == "Preset: Custom");
    CHECK(!away.IsCustom());  // same team, independent side
    home.MoveRow(1); home.Cycle(-1);  // alternate pants
    CHECK(home.Current().pieces[2].variant == 2);
    home.MoveRow(1); CHECK(home.Row() == 0); home.Cycle(1);
    CHECK(!home.IsCustom() && home.Current().preset == 1);
    for (const auto& piece : home.Current().pieces) CHECK(piece.variant == 1 && piece.texture.empty());
    Selection next_game(44, wardrobe, 0);
    CHECK(next_game.Current().preset == 0 && !next_game.IsCustom());
    Selection changed_team(45, MakeWardrobe(45, presets, added), 1);
    CHECK(changed_team.Current().team == 45 && changed_team.Current().pieces[0].team == 45);
    CHECK(changed_team.Choices().pieces[0].size() == 3);
    Selection absent_default(44, wardrobe, 13); CHECK(absent_default.Current().preset == 0);
}
void TextureGroups() {
    std::set<uint32_t> slots;
    for (Part part : {Part::Hat, Part::Jersey, Part::Pants})
        for (const auto& slot : Slots(part)) CHECK(slot.slot < 39 && slots.insert(slot.slot).second);
    CHECK(slots.size() == 20);
    CHECK(!slots.contains(21) && !slots.contains(34));  // dynamically owned lettering textures
    CHECK(Slots(Part::Hat)[0].slot == 1 && std::string(Slots(Part::Hat)[0].tag) == "hat");
    CHECK(Slots(Part::Hat).back().slot == 15 && Slots(Part::Pants).back().slot == 20);
}
void Layout() {
    // Authored synthetic FEL records, no original game asset. Both sides have independent groups.
    std::string text = "VR83\r\n";
    for (const char* side : {"HOME", "AWAY"}) {
        text += " GR:" + std::string(side) + "JERSEY,0,0,0,0,100,100\r\n";
        text += "  TB:JERSEYNAME,1,0,380,272,185,11,1,1,1,1,0,0,test.ffn,1,1,255,255,255,255,255,255,221,117,2,128,128,128,128,128,128,0,0,0,-1,0,0,0,0,0\r\n";
    }
    std::vector<uint8_t> bytes(text.begin(), text.end());
    CHECK(PatchLayout(bytes));
    std::string result(bytes.begin(), bytes.end());
    CHECK(result.find("TB:UNIFORMHAT,1,0,364,252,256,18") != std::string::npos);
    CHECK(result.find("TB:UNIFORMPANTS,1,0,364,292,256,18") != std::string::npos);
    const auto patched = bytes;
    CHECK(!PatchLayout(bytes) && bytes == patched);
    std::string bad = Replace(text, "AWAYJERSEY", "OTHER");
    bytes.assign(bad.begin(), bad.end()); const auto untouched = bytes;
    CHECK(!PatchLayout(bytes) && bytes == untouched);
}
int main(int argc, char** argv) {
    Catalog(); SelectionBehavior(); TextureGroups(); Layout();
    if (argc == 3) {  // optional private original FEL -> patched FEL for local inspection
        std::ifstream in(argv[1], std::ios::binary);
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), {});
        CHECK(PatchLayout(bytes));
        std::ofstream out(argv[2], std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    }
    std::cout << "uniform selection, catalog, material groups and layout checks passed\n";
}

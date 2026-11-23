#pragma once
// NCAA pitch clock state for the gameplay HUD (see pitch_clock.cpp).
namespace game {
int PitchClockSeconds();  // whole seconds remaining, -1 while the clock is not running
bool PitchClockNotice();   // a violation notice is on screen
void DrawPitchClock();    // ImGui overlay; call from the UI frame
void NotePitchClockScene(const char* name); // presentation lifetime; does not change rule-clock state
int DefensiveConferenceAllowance();     // charged defensive conferences allowed so far this game
bool DefensiveConferenceLimitReached(); // the user's allowance is used: the pause menu disables MOUND VISIT
void NoteDefensiveMenu();               // the pause menu's PITCHING/DEFENSE screen is open
void NoteMoundVisitSelected(bool selected);  // that screen's MOUND VISIT row is highlighted
bool MoundVisitAcceptBlocked();
bool TenRunEnded();                    // dev test: the native ten-run check ended a game         // A on the highlighted row is dropped: allowance used
}

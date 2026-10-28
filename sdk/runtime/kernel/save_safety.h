#pragma once
#include <filesystem>
#include <string>

// copies of the player's saves, kept beside them in the user folder:
//   backups\<time>_<why>\        the whole saves folder at launch and exit, the last save_backups kept
//   save_history\<slot>\<time>\  a save slot as it was before the game changed it, the last save_history kept
//   save_lastgood\<slot>\        each slot as it was after its last complete save
// <slot> is UDATA\<title>\<save folder> (or TDATA). A copy is made under a .partial name, its file count and sizes
// checked against the source, then renamed into place; older copies are only removed once the new one checked out.
namespace kernel::saves {

// before boot: the saves folder (mounted as partition 1's parent) and the folder the copies go to
void Configure(const std::filesystem::path& saves, const std::filesystem::path& user);
// the whole saves folder into backups\<time>_<why>; nothing when it equals the newest backup
bool Backup(const char* why);
// before the game truncates, writes, resizes, renames or deletes a file under the saves folder: the first change to
// a slot since its last complete save copies the slot into save_history. writer: a handle that keeps changing it
// until it closes (Closed), so the slot is not taken as complete before then
void BeforeChange(const std::filesystem::path& host, bool writer);
void Closed(const std::filesystem::path& host);
// pending last-good copies done now: before a relaunch or exit
void Flush();
// --restore-backup <name>: the saves folder replaced by a backup (a backups\ folder name, or "lastgood"); the current
// saves are backed up first. message: what happened, for the player
bool Restore(const std::string& name, std::wstring& message);

}  // namespace kernel::saves

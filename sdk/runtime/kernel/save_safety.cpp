#include "kernel/save_safety.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <ctime>
#include <cwctype>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

#include "app/app.h"
#include "core/log.h"
#include "kernel/mods.h"
#include "core/save_compatibility.h"

namespace fs = std::filesystem;

namespace kernel::saves {

namespace {

using Clock = std::chrono::steady_clock;
// a slot counts as completely saved once nothing has written to it for this long and no handle that wrote is open
constexpr auto kSettle = std::chrono::seconds(3);

fs::path g_saves, g_backups, g_history, g_lastgood, g_savemods;
fs::path g_continue;
std::mutex g_lock;          // g_slots
std::mutex g_commit;        // one last-good copy at a time, from the worker or Flush
std::mutex g_stamp_mutex;
std::condition_variable g_wake;

struct Slot {
    fs::path dir;
    int writers = 0;      // open handles that changed it
    bool dirty = false;   // changed since its last complete save; its history copy is made
    uint64_t gen = 0;     // bumped by every change, so a copy taken meanwhile is known to be stale
    Clock::time_point last;
};
std::map<std::wstring, Slot> g_slots;  // upper case slot path relative to the saves folder

std::wstring Upper(std::wstring s) {
    for (wchar_t& c : s) c = wchar_t(std::towupper(c));
    return s;
}

// local time to the millisecond, sorts by name
std::string Stamp() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char s[32];
    std::strftime(s, sizeof(s), "%Y%m%d_%H%M%S", &tm);
    int ms = int(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000);
    return std::string(s) + "_" + char('0' + ms / 100) + char('0' + ms / 10 % 10) + char('0' + ms % 10);
}

fs::path Unique(const fs::path& dir, const std::string& name) {
    std::error_code ec;
    fs::path p = dir / name;
    for (int i = 2; fs::exists(p, ec); i++) p = dir / (name + "-" + std::to_string(i));
    return p;
}

struct Item {
    bool dir;
    uintmax_t size;
    fs::file_time_type time;
};
using Listing = std::map<fs::path, Item>;  // relative path

bool List(const fs::path& root, Listing& out) {
    std::error_code ec;
    out.clear();
    if (!fs::is_directory(root, ec)) return false;
    for (auto it = fs::recursive_directory_iterator(root, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        bool dir = it->is_directory(ec);
        Item item{dir, dir ? 0 : it->file_size(ec), it->last_write_time(ec)};
        if (ec) return false;
        out[it->path().lexically_relative(root)] = item;
    }
    return !ec;
}

uintmax_t Files(const Listing& l, uintmax_t* bytes = nullptr) {
    uintmax_t n = 0, b = 0;
    for (auto& [p, i] : l)
        if (!i.dir) {
            n++;
            b += i.size;
        }
    if (bytes) *bytes = b;
    return n;
}

// same entries and sizes; with times, also the same modification times (copies keep them)
bool Same(const Listing& a, const Listing& b, bool times) {
    if (a.size() != b.size()) return false;
    for (auto ia = a.begin(), ib = b.begin(); ia != a.end(); ++ia, ++ib)
        if (ia->first != ib->first || ia->second.dir != ib->second.dir || ia->second.size != ib->second.size ||
            (times && !ia->second.dir && ia->second.time != ib->second.time))
            return false;
    return true;
}

// src into dst through dst.partial, checked entry by entry before the rename; with replace an existing dst is swapped
// out (to dst.old, removed once the new one is in place, put back if that fails). Nothing is left half copied: on
// any failure the partial folder is removed and dst is as it was. OneDrive folders may have files that are only in
// the cloud; reading them downloads them, and a failed download fails the copy
bool CopyTree(const fs::path& src, const fs::path& dst, bool replace) {
    std::error_code ec;
    Listing from, to;
    if (!List(src, from)) {
        LOGE("saves: cannot list %s", src.string().c_str());
        return false;
    }
    uintmax_t bytes = 0;
    Files(from, &bytes);
    fs::path partial = fs::path(dst) += ".partial";
    fs::create_directories(dst.parent_path(), ec);
    fs::space_info sp = fs::space(dst.parent_path(), ec);
    if (!ec && sp.available < bytes + (16u << 20)) {
        LOGE("saves: not enough free space for %s (%llu bytes)", dst.string().c_str(), (unsigned long long)bytes);
        return false;
    }
    fs::remove_all(partial, ec);  // left by an earlier run that stopped mid copy; always ours
    auto fail = [&](const char* what, const fs::path& p) {
        LOGE("saves: %s %s: %s", what, p.string().c_str(), ec ? ec.message().c_str() : "mismatch");
        std::error_code e;
        fs::remove_all(partial, e);
        return false;
    };
    if (!fs::create_directories(partial, ec) && ec) return fail("cannot create", partial);
    for (auto& [rel, item] : from) {
        if (item.dir) {
            fs::create_directories(partial / rel, ec);
        } else {
            fs::create_directories((partial / rel).parent_path(), ec);
            fs::copy_file(src / rel, partial / rel, fs::copy_options::overwrite_existing, ec);
        }
        if (ec) return fail("cannot copy", src / rel);
    }
    if (!List(partial, to) || !Same(from, to, false)) return fail("copy does not match", src);
    fs::path old = fs::path(dst) += ".old";
    bool had = fs::exists(dst, ec);
    if (had) {
        if (!replace) return fail("already exists", dst);
        fs::remove_all(old, ec);
        fs::rename(dst, old, ec);
        if (ec) return fail("cannot move aside", dst);
    }
    fs::rename(partial, dst, ec);
    if (ec) {
        std::error_code e;
        if (had) fs::rename(old, dst, e);
        return fail("cannot rename into place", dst);
    }
    if (had) fs::remove_all(old, ec);
    return true;
}

// the newest keep folders under dir stay, by name; partial copies are never counted
void Prune(const fs::path& dir, uint32_t keep) {
    std::error_code ec;
    std::vector<fs::path> all;
    for (auto& e : fs::directory_iterator(dir, ec)) {
        std::string ext = e.path().extension().string();
        if (e.is_directory(ec) && ext != ".partial" && ext != ".old") all.push_back(e.path());
    }
    std::sort(all.begin(), all.end());
    for (size_t i = 0; i + keep < all.size(); i++) {
        fs::remove_all(all[i], ec);
        if (ec) LOGE("saves: cannot remove old copy %s: %s", all[i].string().c_str(), ec.message().c_str());
    }
}

// the slot a path under the saves folder belongs to: ...\UDATA|TDATA\<title>\<save folder>[\file]; empty for the
// title's own files (TitleMeta.xbx) and anything outside
fs::path SlotOf(const fs::path& host, std::wstring* key) {
    if (g_saves.empty()) return {};
    fs::path rel = fs::path(host).lexically_normal().lexically_relative(g_saves);
    std::vector<fs::path> parts(rel.begin(), rel.end());
    if(!parts.empty() && parts[0]==L".mvp27-league-stamps")return {};
    for (size_t i = 0; i < parts.size(); i++) {
        if (parts[i] == "..") return {};
        std::wstring p = Upper(parts[i].wstring());
        if (p != L"UDATA" && p != L"TDATA") continue;
        if (parts.size() < i + 3) return {};
        // Title-wide metadata/icons sit beside save directories, not inside a slot.
        const auto leaf=Upper(parts[i+2].wstring());
        if(parts.size()==i+3 && (leaf==L"TITLEMETA.XBX" || leaf==L"TITLEIMAGE.XBX"))return {};
        fs::path slot;
        for (size_t j = 0; j <= i + 2; j++) slot /= parts[j];
        if (key) *key = Upper(slot.wstring());
        return slot;
    }
    return {};
}

// complete: the save's meta file and no empty file; a slot the game is halfway through writing fails this
bool Complete(const Listing& l) {
    bool meta = false;
    for (auto& [p, i] : l) {
        if (i.dir) continue;
        if (!i.size) return false;
        if (Upper(p.filename().wstring()) == L"SAVEMETA.XBX") meta = true;
    }
    return meta;
}

bool CopyLeagueStamp(const fs::path& from,const fs::path& to,const fs::path& rel) {
    auto source=from/L".mvp27-league-stamps"/rel;source+=L".json";
    auto target=to/L".mvp27-league-stamps"/rel;target+=L".json";std::error_code ec;
    if(!fs::exists(source,ec)){fs::remove(target,ec);return !ec;}
    fs::create_directories(target.parent_path(),ec);if(ec)return false;
    fs::copy_file(source,target,fs::copy_options::overwrite_existing,ec);return !ec;
}
bool CommitLastGood(const fs::path& rel) {
    std::lock_guard<std::mutex> lock(g_commit);
    Listing l;
    if (!List(g_saves / rel, l)) return true;  // deleted: the last good copy stays as it was
    if (!Complete(l)) {
        LOGR("saves: %s looks incomplete, last good copy kept", rel.string().c_str());
        return false;
    }
    bool ok = CopyTree(g_saves / rel, g_lastgood / rel, true);
    if(ok)ok=CopyLeagueStamp(g_saves,g_lastgood,rel);
    if (ok) LOGR("saves: last good copy of %s (%llu files)", rel.string().c_str(), (unsigned long long)Files(l));
    return ok;
}

// slots that are complete again: ready ignores the settle time (Flush)
void CommitReady(bool now) {
    std::vector<std::pair<std::wstring, uint64_t>> todo;
    std::vector<fs::path> dirs;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        for (auto& [k, s] : g_slots)
            if (s.dirty && !s.writers && (now || Clock::now() - s.last >= kSettle)) {
                todo.push_back({k, s.gen});
                dirs.push_back(s.dir);
            }
    }
    for (size_t i = 0; i < todo.size(); i++) {
        CommitLastGood(dirs[i]);
        std::lock_guard<std::mutex> lock(g_lock);
        Slot& s = g_slots[todo[i].first];
        if (s.gen == todo[i].second) s.dirty = false;  // else changed during the copy: again once it settles
    }
}

void Worker() {
    for (;;) {
        {
            std::unique_lock<std::mutex> lock(g_lock);
            g_wake.wait_for(lock, std::chrono::seconds(1));
        }
        CommitReady(false);
    }
}

}  // namespace

void Configure(const fs::path& saves, const fs::path& user) {
    g_saves = saves.lexically_normal();
    // a second instance's saves2 gets backups2, save_history2, save_lastgood2
    std::wstring n = g_saves.filename().wstring();
    std::wstring suffix = n.size() > 5 && Upper(n.substr(0, 5)) == L"SAVES" ? n.substr(5) : L"";
    g_backups = user / (L"backups" + suffix);
    g_history = user / (L"save_history" + suffix);
    g_lastgood = user / (L"save_lastgood" + suffix);
    // Outside the mounted Xbox partitions, but inside the backed-up save root.
    // Restoring an older backup must restore its league stamp with its bytes.
    g_savemods = g_saves / L".mvp27-league-stamps";
    static std::once_flag started;
    std::call_once(started, [] { std::thread(Worker).detach(); });
}

bool Allow(const fs::path& host, bool writing, std::string& reason) {
    // File resolution can hold the mod mount lock before entering Allow.
    // Match that order: never acquire the mod lock while holding the stamp lock.
    const auto active=mods::SaveRecord();
    std::lock_guard stamp_lock(g_stamp_mutex);
    auto slot=SlotOf(host,nullptr);if(slot.empty())return true;
    // Native SaveSys hashes the fixed "MVPOptions (Options)" label to this
    // slot. It contains controller/audio/display preferences, not league data.
    if(Upper(slot.filename().wstring())==L"EB8970ABAE91" &&
       Upper(slot.parent_path().filename().wstring())==L"4541008D" &&
       Upper(slot.parent_path().parent_path().filename().wstring())==L"UDATA")return true;
    // The native startup scanner reads names/icons before selecting a save.
    // Inspection must work even when that save's serialized payload is blocked.
    auto relative=host.lexically_normal().lexically_relative(g_saves/slot);
    auto name=Upper(relative.filename().wstring());
    if(!writing && (relative==L"." || (relative.parent_path().empty() &&
       (name==L"SAVEMETA.XBX" || name==L"SAVEIMAGE.XBX" || name==L"TITLEIMAGE.XBX"))))return true;
    auto path=g_savemods/slot;path+=L".json";
    std::ifstream in(path,std::ios::binary);std::string saved((std::istreambuf_iterator<char>(in)),{});
    in.close();
    bool exists=fs::exists(g_saves/slot);
    if(!exists && !writing)return true;
    if(exists && fs::exists(path) && saved.empty()){reason="This save's league stamp is empty or unreadable. The save has been preserved.";return false;}
    if(exists && !saved.empty())return save_compatibility::Compatible(saved,active,reason);
    // A new save folder may already have its Xbox metadata/icon. These are
    // not serialized game data and do not make a new slot a legacy save.
    bool data=false;std::error_code ec;
    auto directory=g_saves/slot;
    if(fs::exists(directory,ec))for(const auto& file:fs::recursive_directory_iterator(directory,ec))if(file.is_regular_file()) {
        auto name=Upper(file.path().filename().wstring());
        if(name!=L"SAVEMETA.XBX" && name!=L"SAVEIMAGE.XBX" && name!=L"TITLEIMAGE.XBX")data=true;
    }
    if(data)return save_compatibility::Compatible({},active,reason);
    if(writing) {
        fs::create_directories(path.parent_path(),ec);auto temp=path;temp+=L".tmp";
        {std::ofstream out(temp,std::ios::binary|std::ios::trunc);out<<active;if(!out){reason="The new save's league stamp could not be written.";return false;}}
        fs::rename(temp,path,ec);if(ec){reason="The new save's league stamp could not be published.";return false;}
    }
    return true;
}

namespace {

// prune false: the backups past keep stay for now (a restore reads one of them)
bool BackupNow(const char* why, uint32_t keep, bool prune) {
    if (g_saves.empty() || !keep) return true;
    Listing now, newest;
    if (!List(g_saves, now) || !Files(now)) return true;  // nothing saved yet
    std::error_code ec;
    fs::create_directories(g_backups, ec);
    std::vector<fs::path> all;
    for (auto& e : fs::directory_iterator(g_backups, ec))
        if (e.is_directory(ec) && e.path().extension() != ".partial" && e.path().extension() != ".old") all.push_back(e.path());
    std::sort(all.begin(), all.end());
    if (!all.empty() && List(all.back(), newest) && Same(now, newest, true)) {
        LOGR("saves: unchanged since backup %s", all.back().filename().string().c_str());
        return true;
    }
    uintmax_t bytes = 0, files = Files(now, &bytes);
    fs::path dst = Unique(g_backups, Stamp() + "_" + why);
    if (!CopyTree(g_saves, dst, false)) {
        LOGE("saves: backup at %s failed, older backups kept", why);
        return false;
    }
    LOGR("saves: backup %s (%llu files, %llu bytes)", dst.filename().string().c_str(), (unsigned long long)files,
         (unsigned long long)bytes);
    if (prune) Prune(g_backups, keep);
    return true;
}

}  // namespace

bool Backup(const char* why) { return BackupNow(why, app::Get().save_backups, true); }

void BeforeChange(const fs::path& host, bool writer) {
    std::wstring key;
    fs::path rel = SlotOf(host, &key);
    if (rel.empty()) return;
    std::lock_guard<std::mutex> lock(g_lock);
    Slot& s = g_slots[key];
    s.dir = rel;
    if (!s.dirty) {
        s.dirty = true;
        // the slot as the last complete save left it; the game waits for this copy (a few MB at most)
        Listing l;
        if (List(g_saves / rel, l) && Files(l)) {
            fs::path dir = g_history / rel;
            if (CopyTree(g_saves / rel, Unique(dir, Stamp()), false)) {
                LOGR("saves: %s archived before the game changes it", rel.string().c_str());
                Prune(dir, std::clamp(app::Get().save_history, 2u, 20u));
            } else {
                LOGE("saves: history copy of %s failed", rel.string().c_str());
            }
        }
    }
    if (writer) s.writers++;
    s.gen++;
    s.last = Clock::now();
}

void Closed(const fs::path& host) {
    std::wstring key;
    fs::path slot = SlotOf(host, &key);
    if (slot.empty()) return;
    // the mods this save was written with (docs/MVPMOD.md 5), beside the slot's copies: the launcher warns when a
    // save's mods that change game data differ from the ones turned on
    std::string record = mods::SaveRecord();
    fs::path side = g_savemods / slot;
    side += L".json";
    std::error_code ec;
    {
        std::lock_guard stamp_lock(g_stamp_mutex);
        std::ifstream old(side, std::ios::binary);
        std::string had((std::istreambuf_iterator<char>(old)), {});old.close();
        if (had != record) {
            fs::create_directories(side.parent_path(), ec);auto temp=side;temp+=L".tmp";
            {std::ofstream out(temp,std::ios::binary|std::ios::trunc);out<<record;}
            fs::rename(temp,side,ec);if(ec)LOGE("saves: league stamp update failed: %s",ec.message().c_str());
        }
    }
    std::lock_guard<std::mutex> lock(g_lock);
    Slot& s = g_slots[key];
    if (s.writers > 0) s.writers--;
    s.gen++;
    s.last = Clock::now();
    g_wake.notify_one();
}

void Flush() {
    if (!g_saves.empty()) CommitReady(true);
}

bool Restore(const std::string& name, std::wstring& message) {
    auto wide = [](const std::string& s) { return fs::path(s).wstring(); };
    std::error_code ec;
    fs::path src = name == "lastgood" ? g_lastgood : g_backups / name;
    Listing l;
    if (name.empty() || name.find_first_of("\\/") != std::string::npos || name.find("..") != std::string::npos ||
        src.extension() == ".partial" || src.extension() == ".old" || !List(src, l) || !Files(l)) {
        message = L"No backup named \"" + wide(name) + L"\". Available (newest last):\n";
        std::vector<fs::path> all;
        for (auto& e : fs::directory_iterator(g_backups, ec))
            if (e.is_directory(ec) && e.path().extension() != ".partial" && e.path().extension() != ".old") all.push_back(e.path());
        std::sort(all.begin(), all.end());
        for (size_t i = all.size() > 15 ? all.size() - 15 : 0; i < all.size(); i++) message += all[i].filename().wstring() + L"\n";
        message += L"lastgood (each save as it was after its last complete save)";
        return false;
    }
    // the saves as they are now, whatever save_backups says: a restore never loses anything
    uint32_t keep = std::max(app::Get().save_backups, 1u);
    if (!BackupNow("before-restore", keep, false)) {
        message = L"The current saves could not be backed up, so nothing was restored. See the log.";
        return false;
    }
    bool ok = true;
    if (name == "lastgood") {
        // each slot's last good copy replaces that slot; slots without one stay
        for (auto& [rel, item] : l)
            if (item.dir && SlotOf(g_saves / rel, nullptr) == rel) {
                bool restored=CopyTree(src / rel, g_saves / rel, true);
                if(restored)restored=CopyLeagueStamp(src,g_saves,rel);
                ok=restored && ok;
            }
    } else {
        ok = CopyTree(src, g_saves, true);
    }
    if (app::Get().save_backups) Prune(g_backups, keep);  // off: the backups there are left alone
    LOGR("saves: restore of %s %s", name.c_str(), ok ? "done" : "failed");
    message = ok ? L"Saves restored from \"" + wide(name) + L"\". The saves you had are in a before-restore backup."
                 : L"Restoring \"" + wide(name) + L"\" failed; see the log. The saves you had are in a before-restore backup.";
    return ok;
}

namespace {
bool Independent(const fs::path& path) {
    std::error_code ec;
    for(auto at=path; !at.empty() && at!=at.root_path(); at=at.parent_path()) {
        auto status=fs::symlink_status(at,ec);
        if(ec==std::errc::no_such_file_or_directory){ec.clear();continue;}
        if(ec || fs::is_symlink(status))return false;
        if(fs::is_regular_file(status) && fs::hard_link_count(at,ec)!=1)return false;
        if(ec)return false;
    }
    return true;
}
bool DynastyName(const fs::path& meta,std::u16string& name) {
    std::ifstream input(meta,std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(input)),{});
    if(bytes.empty() || bytes.size()>4096 || bytes.size()%2)return false;
    std::u16string text;
    for(size_t i=0;i<bytes.size();i+=2)text+=char16_t(uint8_t(bytes[i])|(uint16_t(uint8_t(bytes[i+1]))<<8));
    auto at=text.find(u"Name=");if(at==text.npos)return false;
    name=text.substr(at+5);auto end=name.find_first_of(u"\r\n\0",0,3);if(end!=name.npos)name.resize(end);
    return name.size()<=64 && name.ends_with(u" (Dynasty)");
}
}

bool NewestDynasty(DynastySave& result) {
    result={};std::error_code ec;fs::file_time_type newest=fs::file_time_type::min();
    for(const auto* drive:{"E","F","G"}) {
        const auto root=g_saves/drive/"UDATA";
        if(!fs::is_directory(root,ec)){ec.clear();continue;}
        for(fs::directory_iterator titles(root,ec),end;!ec && titles!=end;titles.increment(ec)) {
            if(Upper(titles->path().filename().wstring())!=L"4541008D" || !Independent(titles->path()))continue;
            for(fs::directory_iterator slots(titles->path(),ec),last;!ec && slots!=last;slots.increment(ec)) {
                const auto path=slots->path(),data=path/"data.dat";Listing listing;std::u16string name;std::string reason;
                if(!Independent(path)||!List(path,listing)||!Complete(listing)||!Independent(data)||
                   !fs::is_regular_file(data,ec)||fs::file_size(data,ec)<268||
                   !DynastyName(path/"SaveMeta.xbx",name)||!Allow(data,false,reason))continue;
                const auto changed=fs::last_write_time(data,ec);if(ec){ec.clear();continue;}
                if(changed>newest || (changed==newest && path<result.slot)){newest=changed;result={path,std::move(name)};}
            }
            ec.clear();
        }
        ec.clear();
    }
    return !result.slot.empty();
}

void LimitContinueTo(const fs::path& slot){std::lock_guard lock(g_lock);g_continue=slot;}
bool ContinueEntryVisible(const fs::path& parent,const std::string& name){
    std::lock_guard lock(g_lock);
    if(g_continue.empty()||Upper(parent.filename().wstring())!=L"4541008D"||Upper(parent.parent_path().filename().wstring())!=L"UDATA")return true;
    auto rel=parent.lexically_relative(g_saves);if(rel.empty()||*rel.begin()=="..")return true;
    return Upper((parent/fs::u8path(name)).wstring())==Upper(g_continue.wstring());
}

bool PublishDynastyAutosave(const std::vector<uint8_t>& data,std::string& reason) {
    // Matches XCreateSaveGame's native twelve-digit name hash.
    std::string name="MVP27 Auto (Dynasty)";
    const auto slotFor=[&](const std::string& label){
        uint64_t hash=0;for(unsigned char c:label)hash=(hash*65536+c)%0xFFFFFFFFFFC5ull;
        std::string directory(12,'0');for(unsigned i=0;i<12;++i)directory[11-i]="0123456789ABCDEF"[(hash>>(4*i))&15];
        return g_saves/"E"/"UDATA"/"4541008d"/directory;
    };
    auto slot=slotFor(name);
    // A new league must not overwrite an older league's automatic save, nor
    // prevent the current dynasty from saving. Give it a separate native slot.
    if(fs::exists(slot) && !Allow(slot/"data.dat",false,reason)){
        uint64_t identity=14695981039346656037ull;
        for(unsigned char c:mods::SaveRecord()){identity^=c;identity*=1099511628211ull;}
        name="MVP27 Auto "+std::to_string(identity)+" (Dynasty)";
        slot=slotFor(name);reason.clear();
    }
    const auto file=slot/"data.dat",marker=slot/"mvp27-autosave.txt";
    const std::string ownership="MVP27_AUTOSAVE_1\n";
    try {
        if(data.size()<268||data.size()>64*1024*1024||!Independent(slot)||!Independent(file)||!Independent(marker))
            throw std::runtime_error("Autosave payload/path is invalid");
        if(fs::exists(slot)) {
            std::ifstream input(marker,std::ios::binary);std::string owned((std::istreambuf_iterator<char>(input)),{});
            if(owned!=ownership)throw std::runtime_error("Reserved autosave slot is occupied; existing save preserved");
        }
        if(!Allow(file,true,reason))return false;
        const auto stage=Unique(g_backups.parent_path(),"autosave-staging-"+Stamp());
        if(!Independent(stage))throw std::runtime_error("Autosave staging path is invalid");
        fs::create_directories(stage);
        struct Cleanup {fs::path path;~Cleanup(){std::error_code ec;fs::remove_all(path,ec);}} cleanup{stage};
        auto write=[&](const char* leaf,const std::vector<uint8_t>& bytes){
            const auto path=stage/leaf;
            {std::ofstream output(path,std::ios::binary);output.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());output.flush();if(!output)throw std::runtime_error("Autosave write failed");}
            std::ifstream input(path,std::ios::binary);std::vector<uint8_t> checked((std::istreambuf_iterator<char>(input)),{});
            if(checked!=bytes)throw std::runtime_error("Autosave readback differs");
        };
        std::vector<uint8_t> meta{0xff,0xfe};for(unsigned char c:std::string("Name=")+name+"\r\n"){meta.push_back(c);meta.push_back(0);}
        write("SaveMeta.xbx",meta);write("data.dat",data);write("mvp27-autosave.txt",{ownership.begin(),ownership.end()});
        BeforeChange(file,false);
        {std::lock_guard lock(g_commit);fs::create_directories(slot.parent_path());if(!CopyTree(stage,slot,true))throw std::runtime_error("Autosave publication failed; previous slot preserved");}
        Flush();LOGR("saves: native dynasty autosave committed (%zu bytes)",data.size());return true;
    }catch(const std::exception& e){reason=e.what();LOGE("saves: autosave failed: %s",reason.c_str());return false;}
}

}  // namespace kernel::saves

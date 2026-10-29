using System.Globalization;
using System.Text;

namespace MVPSetup;

/// <summary>
/// The player's folder as the port's runtime finds it (sdk/runtime/app/main.cpp UserDir): beside the exe when
/// portable.txt is there, else Documents\MVP06 Recomp. Holds the settings file, saves and their copies
/// (sdk/runtime/kernel/save_safety.h).
/// </summary>
sealed class UserData(string installDir)
{
    public string Dir { get; } = File.Exists(Path.Combine(installDir, "portable.txt"))
        ? installDir
        : Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments), Game.UserFolder);

    public string SettingsPath => Path.Combine(Dir, Game.SettingsFile);
    public string SavesDir => Path.Combine(Dir, "saves");
    public string BackupsDir => Path.Combine(Dir, "backups");
    public string LastGoodDir => Path.Combine(Dir, "save_lastgood");
    public string HistoryDir => Path.Combine(Dir, "save_history");

    public sealed record Save(string Name, string Slot, DateTime Changed, int History);
    public sealed record Backup(string Folder, DateTime Time, string Why, long Bytes);

    /// <summary>Each save slot (a folder under UDATA\&lt;title&gt;) with the name its SaveMeta.xbx gives it.</summary>
    public List<Save> Saves()
    {
        var list = new List<Save>();
        foreach (var drive in new[] { "E", "F", "G" })
        {
            var udata = Path.Combine(SavesDir, drive, "UDATA");
            if (!Directory.Exists(udata)) continue;
            foreach (var title in Directory.EnumerateDirectories(udata))
            foreach (var slot in Directory.EnumerateDirectories(title))
            {
                var rel = Path.GetRelativePath(SavesDir, slot);
                var meta = Path.Combine(slot, "SaveMeta.xbx");
                var name = File.Exists(meta) ? MetaName(meta) ?? Path.GetFileName(slot) : Path.GetFileName(slot);
                var changed = Directory.EnumerateFiles(slot, "*", SearchOption.AllDirectories).Select(File.GetLastWriteTime).DefaultIfEmpty(Directory.GetLastWriteTime(slot)).Max();
                var hist = Path.Combine(HistoryDir, rel);
                var history = Directory.Exists(hist) ? Directory.EnumerateDirectories(hist).Count() : 0;
                list.Add(new Save(name, rel, changed, history));
            }
        }
        return list.OrderByDescending(s => s.Changed).ToList();
    }

    // UTF-16 "Name=..." lines, as the Xbox dashboard reads them
    static string? MetaName(string path)
    {
        try
        {
            var text = Encoding.Unicode.GetString(File.ReadAllBytes(path)).TrimStart('﻿');
            foreach (var line in text.Split('\n'))
                if (line.StartsWith("Name=", StringComparison.Ordinal)) return line[5..].Trim('\r', '\0', ' ');
        }
        catch (IOException) { }
        return null;
    }

    /// <summary>The backups\ folders, newest first: "&lt;yyyyMMdd_HHmmss_fff&gt;_&lt;why&gt;".</summary>
    public List<Backup> Backups()
    {
        if (!Directory.Exists(BackupsDir)) return [];
        var list = new List<Backup>();
        foreach (var d in Directory.EnumerateDirectories(BackupsDir))
        {
            var name = Path.GetFileName(d);
            if (name.EndsWith(".partial", StringComparison.OrdinalIgnoreCase)) continue;
            var parts = name.Split('_', 4);
            var time = parts.Length >= 3 && DateTime.TryParseExact($"{parts[0]}_{parts[1]}", "yyyyMMdd_HHmmss", CultureInfo.InvariantCulture, DateTimeStyles.None, out var t)
                ? t : Directory.GetCreationTime(d);
            var why = parts.Length == 4 ? parts[3] : "";
            var bytes = Directory.EnumerateFiles(d, "*", SearchOption.AllDirectories).Sum(f => new FileInfo(f).Length);
            list.Add(new Backup(name, time, why, bytes));
        }
        return list.OrderByDescending(b => b.Time).ToList();
    }

    public bool HasLastGood => Directory.Exists(LastGoodDir) && Directory.EnumerateFileSystemEntries(LastGoodDir).Any();
}

/// <summary>
/// mvp06-settings.toml as plain "key = value" lines, edited in place: lines the launcher doesn't know (and comments)
/// are kept, and keys it sets that the file lacks are added at the end. The runtime reads it at start
/// (sdk/runtime/app/settings.cpp) and writes it whenever PC Options change, so edit while the game is closed.
/// </summary>
sealed class SettingsFile
{
    readonly string path;
    readonly List<string> lines;

    public SettingsFile(string path)
    {
        this.path = path;
        lines = File.Exists(path) ? [.. File.ReadAllLines(path)] : [];
    }

    int Find(string key)
    {
        for (var i = 0; i < lines.Count; i++)
        {
            var eq = lines[i].IndexOf('=');
            if (eq > 0 && !lines[i].TrimStart().StartsWith('#') && lines[i][..eq].Trim() == key) return i;
        }
        return -1;
    }

    /// <summary>The value as written, quotes removed; null when the file doesn't set it.</summary>
    public string? Get(string key)
    {
        var i = Find(key);
        if (i < 0) return null;
        var v = lines[i][(lines[i].IndexOf('=') + 1)..].Trim();
        var hash = v.StartsWith('\'') || v.StartsWith('"') ? -1 : v.IndexOf('#');
        if (hash >= 0) v = v[..hash].Trim();
        if (v.Length >= 2 && (v[0] == '\'' || v[0] == '"') && v[^1] == v[0]) v = v[1..^1];
        return v;
    }

    public void Set(string key, string value, bool quoted)
    {
        var text = $"{key} = {(quoted ? "'" + value.Replace("'", "") + "'" : value)}";
        var i = Find(key);
        if (i >= 0) lines[i] = text;
        else lines.Add(text);
    }

    public void Save()
    {
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        File.WriteAllLines(path, lines);
    }
}

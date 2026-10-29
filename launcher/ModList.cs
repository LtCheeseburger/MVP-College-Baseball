using System.IO.Compression;
using System.Text.Json;
using System.Text.Json.Nodes;

namespace MVPSetup;

/// <summary>
/// The mods folder beside MVP06.exe, as the runtime reads it (docs/MVPMOD.md, sdk/runtime/kernel/mods.cpp): every *.mvpmod and every
/// folder holding mod.json, turned on and ordered by mods.json. Later in the order wins. The launcher writes
/// mods.json; the game reads it at its next start.
/// </summary>
sealed class ModList
{
    const int Format = 1, Layout = 1;

    public sealed class Mod
    {
        public required string Path { get; init; }
        public string Id { get; init; } = "";
        public string Name { get; init; } = "";
        public string Version { get; init; } = "";
        public string Author { get; init; } = "";
        public string Description { get; init; } = "";
        public bool AffectsSaves { get; init; }
        public List<(string Id, string Version)> Requires { get; init; } = [];
        public List<string> Conflicts { get; init; } = [];
        public HashSet<string> Files { get; init; } = new(StringComparer.OrdinalIgnoreCase);  // game paths, '/'
        public string? Error { get; init; }  // why it can't be turned on
        public bool Enabled { get; set; }
        public string FileName => System.IO.Path.GetFileName(Path);
    }

    public string Dir { get; }
    public List<Mod> Mods { get; } = [];  // in load order

    public ModList(string installDir)
    {
        Dir = System.IO.Path.Combine(installDir, "mods");
        Reload();
    }

    public void Reload()
    {
        Mods.Clear();
        if (!Directory.Exists(Dir)) return;
        var found = new List<Mod>();
        foreach (var f in Directory.EnumerateFiles(Dir, "*.mvpmod")) found.Add(Read(f));
        foreach (var d in Directory.EnumerateDirectories(Dir))
            if (File.Exists(System.IO.Path.Combine(d, "mod.json"))) found.Add(Read(d));
        // a second mod with one id can't be on: the runtime refuses it too
        var seen = new HashSet<string>();
        for (var i = 0; i < found.Count; i++)
            if (found[i].Error == null && !seen.Add(found[i].Id))
                found[i] = Broken(found[i].Path, $"another mod already uses the id \"{found[i].Id}\"");

        var order = new List<(string Id, bool On)>();
        try
        {
            if (JsonNode.Parse(File.ReadAllText(System.IO.Path.Combine(Dir, "mods.json")))?["order"] is JsonArray arr)
                foreach (var o in arr)
                    if (o?["id"]?.GetValue<string>() is { } id) order.Add((id, o["enabled"]?.GetValue<bool>() == true));
        }
        catch (Exception e) when (e is IOException or JsonException or InvalidOperationException) { }
        foreach (var (id, on) in order)
        {
            var m = found.FirstOrDefault(x => x.Error == null && x.Id == id);
            if (m == null) continue;
            m.Enabled = on;
            Mods.Add(m);
            found.Remove(m);
        }
        Mods.AddRange(found.OrderBy(m => m.Name, StringComparer.OrdinalIgnoreCase));  // new ones go last, off
    }

    public void Save()
    {
        Directory.CreateDirectory(Dir);
        var arr = new JsonArray();
        foreach (var m in Mods.Where(m => m.Error == null)) arr.Add(new JsonObject { ["id"] = m.Id, ["enabled"] = m.Enabled });
        var json = new JsonObject { ["format"] = Format, ["order"] = arr };
        var tmp = System.IO.Path.Combine(Dir, "mods.json.tmp");
        File.WriteAllText(tmp, json.ToJsonString(new JsonSerializerOptions { WriteIndented = true }));
        File.Move(tmp, System.IO.Path.Combine(Dir, "mods.json"), true);
    }

    /// <summary>Copies a .mvpmod (or a mod folder) into the mods folder; the new mod is added last, off.</summary>
    public Mod Add(string source)
    {
        var mod = Read(source);
        if (mod.Error != null) return mod;
        Directory.CreateDirectory(Dir);
        var to = System.IO.Path.Combine(Dir, System.IO.Path.GetFileName(source.TrimEnd('\\', '/')));
        if (Directory.Exists(source)) CopyDir(source, to);
        else File.Copy(source, to, true);
        return mod;
    }

    static void CopyDir(string from, string to)
    {
        foreach (var f in Directory.EnumerateFiles(from, "*", SearchOption.AllDirectories))
        {
            var dest = System.IO.Path.Combine(to, System.IO.Path.GetRelativePath(from, f));
            Directory.CreateDirectory(System.IO.Path.GetDirectoryName(dest)!);
            File.Copy(f, dest, true);
        }
    }

    /// <summary>What's wrong with turning these mods on in this order, one line each; empty when nothing is.</summary>
    public List<string> Problems()
    {
        var on = Mods.Where(m => m.Enabled && m.Error == null).ToList();
        var list = new List<string>();
        foreach (var m in on)
        {
            foreach (var (id, want) in m.Requires)
            {
                var dep = on.FirstOrDefault(x => x.Id == id);
                if (dep == null) list.Add($"{m.Name} needs \"{id}\"{(want != "" ? " " + want : "")}, which isn't on. The game will leave {m.Name} off.");
                else if (want != "" && !VersionOk(dep.Version, want)) list.Add($"{m.Name} needs {id} {want}; {dep.Version} is on. The game will leave {m.Name} off.");
            }
            foreach (var c in m.Conflicts)
                if (on.FirstOrDefault(x => x.Id == c) is { } other)
                    list.Add($"{m.Name} and {other.Name} say they can't be used together. The game will leave the later one off.");
        }
        return list;
    }

    /// <summary>Files more than one enabled mod changes, and which one wins (the later).</summary>
    public List<(string File, Mod Winner, List<Mod> Losers)> Overlaps()
    {
        var by = new Dictionary<string, List<Mod>>(StringComparer.OrdinalIgnoreCase);
        foreach (var m in Mods.Where(m => m.Enabled && m.Error == null))
        foreach (var f in m.Files)
        {
            if (!by.TryGetValue(f, out var l)) by[f] = l = [];
            l.Add(m);
        }
        return by.Where(kv => kv.Value.Count > 1).OrderBy(kv => kv.Key)
            .Select(kv => (kv.Key, kv.Value[^1], kv.Value.Take(kv.Value.Count - 1).ToList())).ToList();
    }

    static Mod Broken(string path, string why) => new() { Path = path, Name = System.IO.Path.GetFileName(path), Error = why };

    // the same checks the runtime makes (mods.cpp OpenMod)
    static Mod Read(string path)
    {
        try
        {
            string text;
            var files = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            if (Directory.Exists(path))
            {
                var meta = System.IO.Path.Combine(path, "mod.json");
                if (!File.Exists(meta)) return Broken(path, "the folder has no mod.json");
                text = File.ReadAllText(meta);
                var root = System.IO.Path.Combine(path, "files");
                if (Directory.Exists(root))
                    foreach (var f in Directory.EnumerateFiles(root, "*", SearchOption.AllDirectories))
                        files.Add(System.IO.Path.GetRelativePath(root, f).Replace('\\', '/'));
            }
            else
            {
                using var zip = ZipFile.OpenRead(path);
                var meta = zip.GetEntry("mod.json");
                if (meta == null) return Broken(path, "it has no mod.json");
                using (var r = new StreamReader(meta.Open())) text = r.ReadToEnd();
                foreach (var e in zip.Entries)
                {
                    var n = e.FullName.Replace('\\', '/');
                    if (n.StartsWith("files/", StringComparison.OrdinalIgnoreCase) && !n.EndsWith('/')) files.Add(n[6..]);
                }
            }
            var j = JsonNode.Parse(text) as JsonObject;
            if (j == null) return Broken(path, "mod.json isn't a JSON object");
            string S(string k) => j[k] is JsonValue v && v.TryGetValue<string>(out var s) ? s : "";
            int N(string k) => j[k] is JsonValue v && v.TryGetValue<double>(out var d) ? (int)d : 0;
            var id = S("id");
            var sha = (j["game"]?["xbeSha1"] as JsonValue)?.TryGetValue<string>(out var x) == true ? x : "";
            string? why =
                N("format") > Format ? "it needs a newer version of the port" :
                N("format") < 1 ? "its mod.json has no format number" :
                id == "" || id.Any(c => !(char.IsAsciiLetterLower(c) || char.IsAsciiDigit(c) || c == '-')) ? "its id isn't lower case letters, digits and '-'" :
                N("layout") != Layout ? "it was made for another install layout" :
                !string.Equals(sha, Game.XbeSha1, StringComparison.OrdinalIgnoreCase) ? "it was made for another release of the game" :
                null;
            if (why != null) return Broken(path, why);
            var requires = new List<(string, string)>();
            if (j["requires"] is JsonArray req)
                foreach (var r in req.OfType<JsonObject>())
                    requires.Add((r["id"]?.GetValue<string>() ?? "", r["version"]?.GetValue<string>() ?? ""));
            return new Mod
            {
                Path = path,
                Id = id,
                Name = S("name") is { Length: > 0 } name ? name : id,
                Version = S("version") is { Length: > 0 } ver ? ver : "0.0.0",
                Author = S("author"),
                Description = S("description"),
                AffectsSaves = j["affectsSaves"] is JsonValue a && a.TryGetValue<bool>(out var b) && b,
                Requires = requires,
                Conflicts = j["conflicts"] is JsonArray c ? c.Select(v => v?.GetValue<string>() ?? "").Where(v => v != "").ToList() : [],
                Files = files,
            };
        }
        catch (Exception e) when (e is IOException or InvalidDataException or JsonException or InvalidOperationException or UnauthorizedAccessException)
        {
            return Broken(path, "it can't be read: " + e.Message);
        }
    }

    // semver against ">=1.0.0" and the like; a bare version means at least that one (mods.cpp VersionOk)
    static bool VersionOk(string have, string want)
    {
        var i = 0;
        while (i < want.Length && "<>= ".Contains(want[i])) i++;
        var op = want[..i].Replace(" ", "");
        static int[] Parts(string v) => [.. v.Split('.').Concat(["0", "0", "0"]).Take(3).Select(p => int.TryParse(new string(p.TakeWhile(char.IsDigit).ToArray()), out var n) ? n : 0)];
        var h = Parts(have);
        var w = Parts(want[i..]);
        var cmp = h[0] != w[0] ? h[0].CompareTo(w[0]) : h[1] != w[1] ? h[1].CompareTo(w[1]) : h[2].CompareTo(w[2]);
        return op switch { "" or ">=" => cmp >= 0, ">" => cmp > 0, "=" or "==" => cmp == 0, "<=" => cmp <= 0, "<" => cmp < 0, _ => false };
    }
}

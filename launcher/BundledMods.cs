using System.IO.Compression;
using System.Security.Cryptography;
using System.Text.Json;
using System.Text.Json.Nodes;

namespace MVPSetup;

// Release-managed cache is a port file; player toggles and other mods are never port files.
static class BundledMods
{
    const string Id = "mvp27-ui";
    public const string CachePath = "bundled-mods/mvp27-ui.mvpmod";
    public static void Install(string dir, Action<string>? beforeWrite = null)
    {
        var cache = Path.Combine(dir, CachePath);
        if (!File.Exists(cache)) return; // Older releases have no bundled UI.
        var manifest = PortUpdater.ReadManifest(dir) ?? throw new InvalidDataException("Missing port manifest for bundled UI.");
        var entry = manifest.Files.SingleOrDefault(f => f.Path.Replace('\\', '/') == CachePath)
                    ?? throw new InvalidDataException("Bundled UI is not in the port manifest.");
        using (var stream = File.OpenRead(cache))
            if (stream.Length != entry.Size || Convert.ToHexStringLower(SHA256.HashData(stream)) != entry.Sha256)
                throw new InvalidDataException("Bundled UI cache is damaged. Extract the release again or update before repairing.");
        using (var zip = ZipFile.OpenRead(cache))
        using (var reader = new StreamReader((zip.GetEntry("mod.json") ?? throw new InvalidDataException("UI mod has no manifest.")).Open()))
        {
            var mod = JsonNode.Parse(reader.ReadToEnd());
            if ((string?)mod?["id"] != Id || (bool?)mod?["affectsSaves"] != false ||
                (string?)mod?["game"]?["xbeSha1"] != Game.XbeSha1)
                throw new InvalidDataException("Bundled UI metadata doesn't match this game.");
        }
        var mods = Path.Combine(dir, "mods"); Directory.CreateDirectory(mods);
        // A developer folder is editable user material: preserve it and avoid duplicate IDs.
        var folder = Directory.EnumerateDirectories(mods).Any(path => {
            var json = Path.Combine(path, "mod.json");
            try { return File.Exists(json) && (string?)JsonNode.Parse(File.ReadAllText(json))?["id"] == Id; }
            catch (JsonException) { return false; }
        });
        if (!folder)
        {
            var target = Path.Combine(mods, Id + ".mvpmod");
            var same = false;
            if (File.Exists(target)) { using var stream = File.OpenRead(target); same = Convert.ToHexStringLower(SHA256.HashData(stream)) == entry.Sha256; }
            if (!same)
            {
                File.Copy(cache, target + ".tmp", true);
                try { beforeWrite?.Invoke("mods/" + Id + ".mvpmod"); File.Move(target + ".tmp", target, true); }
                finally { if (File.Exists(target + ".tmp")) File.Delete(target + ".tmp"); }
            }
        }
        var configPath = Path.Combine(mods, "mods.json");
        var config = File.Exists(configPath) ? JsonNode.Parse(File.ReadAllText(configPath)) as JsonObject
            ?? throw new InvalidDataException("Invalid mods.json; existing toggles were preserved.") : new JsonObject { ["format"] = 1, ["order"] = new JsonArray() };
        var order = config["order"] as JsonArray ?? throw new InvalidDataException("Invalid mod order.");
        if (!order.Any(row => (string?)row?["id"] == Id))
        {
            var defaults = Path.Combine(dir, "bundled-mods/defaults.json");
            bool enabled = !File.Exists(defaults) || (bool?)JsonNode.Parse(File.ReadAllText(defaults))?[Id] == true;
            order.Add(new JsonObject { ["id"] = Id, ["enabled"] = enabled });
            File.WriteAllText(configPath + ".tmp", config.ToJsonString(new JsonSerializerOptions { WriteIndented = true }));
            try { beforeWrite?.Invoke("mods/mods.json"); File.Move(configPath + ".tmp", configPath, true); }
            finally { if (File.Exists(configPath + ".tmp")) File.Delete(configPath + ".tmp"); }
        }
    }
}

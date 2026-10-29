using System.IO.Compression;
using System.Net.Http;
using System.Security.Cryptography;
using System.Text.Json;
using System.Text.Json.Nodes;

namespace MVPSetup;

/// <summary>A newer port release on GitHub: its version, notes and the release zip with its .sha256.</summary>
public sealed record PortRelease(string Version, string Tag, string Name, string Notes, string ZipUrl, string ZipName, string ShaUrl, string PageUrl);

/// <summary>
/// Updates an installed port from its GitHub releases (pre-releases included): the newest release's zip is downloaded
/// and checked against its .sha256, its "Port Files" are checked against their port-manifest.json, then swapped into the
/// install. Every replaced or removed file is moved to .update-backup first, so a failure puts everything back. The
/// running Launcher.exe is renamed out of the way (Windows allows that), so the launcher updates itself too.
/// Only files a port manifest names are touched: game\, assets\, mods\, the built game code and compiler\work never are.
/// </summary>
public static class PortUpdater
{
    public const string Repo = "LtCheeseburger/MVP-College-Baseball";
    public const string ZipPrefix = "MVP-27-College-Baseball-v";
    public const string ManifestName = "port-manifest.json";
    public const string VersionName = "version.txt";
    const string Staging = ".update-staging", Backup = ".update-backup", Journal = ".update-journal";

    static readonly HttpClient Http = MakeClient();

    static HttpClient MakeClient()
    {
        var c = new HttpClient { Timeout = TimeSpan.FromMinutes(30) };
        c.DefaultRequestHeaders.UserAgent.ParseAdd("MVP06-Launcher");
        c.DefaultRequestHeaders.Accept.ParseAdd("application/vnd.github+json");
        return c;
    }

    /// <summary>The installed port version (version.txt); "0.0.0" when the install has none.</summary>
    public static string InstalledVersion(string installDir)
    {
        try { var v = File.ReadAllText(Path.Combine(installDir, VersionName)).Trim(); return v == "" ? "0.0.0" : v; }
        catch (IOException) { return "0.0.0"; }
        catch (UnauthorizedAccessException) { return "0.0.0"; }
    }

    // ---- versions: semver, a pre-release ("0.2.0-beta") sorting below its release ("0.2.0") ----

    public static int CompareVersions(string a, string b)
    {
        static (int[] Core, string? Pre) Parse(string v)
        {
            v = v.Trim().TrimStart('v', 'V');
            var plus = v.IndexOf('+');
            if (plus >= 0) v = v[..plus];
            var dash = v.IndexOf('-');
            var pre = dash >= 0 ? v[(dash + 1)..] : null;
            var core = (dash >= 0 ? v[..dash] : v).Split('.').Select(p => int.TryParse(p, out var n) ? n : 0).Concat([0, 0, 0]).Take(3).ToArray();
            return (core, string.IsNullOrEmpty(pre) ? null : pre);
        }
        var (ca, pa) = Parse(a);
        var (cb, pb) = Parse(b);
        for (var i = 0; i < 3; i++) if (ca[i] != cb[i]) return ca[i].CompareTo(cb[i]);
        if (pa == pb) return 0;
        if (pa is null) return 1;
        if (pb is null) return -1;
        var xa = pa.Split('.');
        var xb = pb.Split('.');
        for (var i = 0; i < Math.Min(xa.Length, xb.Length); i++)
        {
            var na = int.TryParse(xa[i], out var ia);
            var nb = int.TryParse(xb[i], out var ib);
            var c = na && nb ? ia.CompareTo(ib) : na ? -1 : nb ? 1 : string.CompareOrdinal(xa[i], xb[i]);
            if (c != 0) return c;
        }
        return xa.Length.CompareTo(xb.Length);
    }

    // ---- checking ----

    /// <summary>The highest-versioned release (pre-releases count, drafts don't) when it's newer than <paramref name="installed"/>; else null.</summary>
    public static async Task<PortRelease?> CheckAsync(string installed, CancellationToken ct = default)
    {
        using var check = CancellationTokenSource.CreateLinkedTokenSource(ct);
        check.CancelAfter(TimeSpan.FromSeconds(15));
        using var doc = JsonDocument.Parse(await Http.GetStringAsync($"https://api.github.com/repos/{Repo}/releases?per_page=10", check.Token));
        PortRelease? best = null;
        foreach (var r in doc.RootElement.EnumerateArray())
        {
            if (r.TryGetProperty("draft", out var draft) && draft.GetBoolean()) continue;
            var tag = r.GetProperty("tag_name").GetString() ?? "";
            string? zip = null, zipName = null, sha = null;
            foreach (var a in r.GetProperty("assets").EnumerateArray())
            {
                var name = a.GetProperty("name").GetString() ?? "";
                var url = a.GetProperty("browser_download_url").GetString() ?? "";
                if (!name.StartsWith(ZipPrefix, StringComparison.OrdinalIgnoreCase)) continue;
                if (name.EndsWith(".zip", StringComparison.OrdinalIgnoreCase)) { zip = url; zipName = name; }
                else if (name.EndsWith(".zip.sha256", StringComparison.OrdinalIgnoreCase)) sha = url;
            }
            if (zip is null || sha is null) continue;  // not a release the launcher can install
            var version = tag.TrimStart('v', 'V');
            if (best is not null && CompareVersions(version, best.Version) <= 0) continue;
            best = new PortRelease(version, tag, r.GetProperty("name").GetString() ?? tag, r.TryGetProperty("body", out var b) ? b.GetString() ?? "" : "",
                zip, zipName!, sha, r.GetProperty("html_url").GetString() ?? "");
        }
        return best is not null && CompareVersions(best.Version, installed) > 0 ? best : null;
    }

    /// <summary>Why files in <paramref name="installDir"/> can't be replaced; null when they can.</summary>
    public static string? InstallProblem(string installDir)
    {
        try
        {
            var probe = Path.Combine(installDir, ".update-probe");
            File.WriteAllText(probe, "");
            File.Delete(probe);
            return null;
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            return $"The launcher can't write to {installDir} ({e.Message}). Start it as administrator, or install the update with setup.";
        }
    }

    // ---- downloading ----

    /// <summary>Downloads the release zip into the user's temp folder and checks its SHA-256; returns the zip's path.</summary>
    public static async Task<string> DownloadAsync(PortRelease release, IProgress<(long Done, long? Total)> progress, CancellationToken ct = default)
    {
        var work = Path.Combine(Path.GetTempPath(), "MVP06-Recomp-update");
        Directory.CreateDirectory(work);
        var zipPath = Path.Combine(work, release.ZipName);
        var expected = (await Http.GetStringAsync(release.ShaUrl, ct)).Trim().Split(' ', '\t', '*')[0].ToLowerInvariant();
        // a finished download from an earlier try is used again
        if (File.Exists(zipPath) && await Sha256(zipPath, ct) == expected) return zipPath;
        var part = zipPath + ".part";
        using (var resp = await Http.GetAsync(release.ZipUrl, HttpCompletionOption.ResponseHeadersRead, ct))
        {
            resp.EnsureSuccessStatusCode();
            var total = resp.Content.Headers.ContentLength;
            await using var src = await resp.Content.ReadAsStreamAsync(ct);
            await using var dst = File.Create(part);
            var buf = new byte[1 << 16];
            long done = 0, shown = 0;
            int n;
            while ((n = await src.ReadAsync(buf, ct)) > 0)
            {
                await dst.WriteAsync(buf.AsMemory(0, n), ct);
                done += n;
                if (done - shown >= 1 << 19) { shown = done; progress.Report((done, total)); }
            }
            progress.Report((done, total));
        }
        var actual = await Sha256(part, ct);
        if (actual != expected) { File.Delete(part); throw new InvalidDataException("The download is damaged (its SHA-256 doesn't match the release). Try again later."); }
        File.Move(part, zipPath, true);
        return zipPath;
    }

    static async Task<string> Sha256(string path, CancellationToken ct)
    {
        await using var f = File.OpenRead(path);
        return Convert.ToHexStringLower(await SHA256.HashDataAsync(f, ct));
    }

    // ---- applying ----

    public sealed record ManifestFile(string Path, long Size, string Sha256);

    public static (string Version, List<ManifestFile> Files)? ReadManifest(string dir)
    {
        var path = Path.Combine(dir, ManifestName);
        if (!File.Exists(path)) return null;
        var j = JsonNode.Parse(File.ReadAllText(path)) as JsonObject ?? throw new InvalidDataException($"{ManifestName} isn't valid.");
        var files = new List<ManifestFile>();
        foreach (var f in j["files"] as JsonArray ?? [])
            files.Add(new ManifestFile((f?["path"]?.GetValue<string>() ?? "").Replace('/', '\\'), f?["size"]?.GetValue<long>() ?? 0, f?["sha256"]?.GetValue<string>()?.ToLowerInvariant() ?? ""));
        return (j["version"]?.GetValue<string>() ?? "", files);
    }

    // paths an update may never write or remove, whatever a manifest says
    static bool Protected(string rel)
    {
        var r = rel.Replace('/', '\\').TrimStart('\\');
        if (r.Contains("..") || Path.IsPathRooted(r)) return true;
        string[] folders = ["game\\", "assets\\", "mods\\", "compiler\\work\\", Staging + "\\", Backup + "\\"];
        if (folders.Any(f => r.StartsWith(f, StringComparison.OrdinalIgnoreCase))) return true;
        return r.Equals("portable.txt", StringComparison.OrdinalIgnoreCase) || r.Equals(Journal, StringComparison.OrdinalIgnoreCase)
               || r.EndsWith(".dll", StringComparison.OrdinalIgnoreCase) && !r.Contains('\\') && r.StartsWith("MVP06", StringComparison.OrdinalIgnoreCase);
    }

    /// <summary>
    /// Installs the release zip's port files over <paramref name="installDir"/>: unpack, check, swap with backups.
    /// On any failure the install is put back as it was and the exception is rethrown.
    /// </summary>
    public static void Apply(string installDir, string zipPath, IProgress<string>? progress = null)
    {
        var staging = Path.Combine(installDir, Staging);
        var backup = Path.Combine(installDir, Backup);
        if (Directory.Exists(staging)) Directory.Delete(staging, true);
        if (Directory.Exists(backup)) Directory.Delete(backup, true);

        progress?.Report("Unpacking");
        ZipFile.ExtractToDirectory(zipPath, staging);
        var port = Directory.EnumerateDirectories(staging, "Port Files", SearchOption.AllDirectories).FirstOrDefault()
                   ?? throw new InvalidDataException("The release zip has no \"Port Files\" folder.");
        var manifest = ReadManifest(port) ?? throw new InvalidDataException($"The release has no {ManifestName}.");

        progress?.Report("Checking the new files");
        foreach (var f in manifest.Files)
        {
            if (Protected(f.Path)) throw new InvalidDataException($"The release lists {f.Path}, which an update may not change.");
            var p = Path.Combine(port, f.Path);
            if (!File.Exists(p) || new FileInfo(p).Length != f.Size || Convert.ToHexStringLower(SHA256.HashData(File.ReadAllBytes(p))) != f.Sha256)
                throw new InvalidDataException($"{f.Path} in the download doesn't match its release list.");
        }

        var old = ReadManifest(installDir);
        var keep = manifest.Files.Select(f => f.Path).ToHashSet(StringComparer.OrdinalIgnoreCase);
        var remove = old?.Files.Select(f => f.Path).Where(p => !keep.Contains(p) && !Protected(p)).ToList() ?? [];
        // what was swapped, so it can be put back: (installed path, backup path or null when it was new)
        var done = new List<(string Dst, string? Saved)>();
        File.WriteAllText(Path.Combine(installDir, Journal), "updating");
        try
        {
            progress?.Report("Installing");
            void SaveAside(string dst, string rel)
            {
                if (!File.Exists(dst)) { done.Add((dst, null)); return; }
                var saved = Path.Combine(backup, rel);
                Directory.CreateDirectory(Path.GetDirectoryName(saved)!);
                File.Move(dst, saved);  // a running Launcher.exe can be renamed like this
                done.Add((dst, saved));
            }
            foreach (var f in manifest.Files.Append(new ManifestFile(ManifestName, 0, "")))
            {
                var dst = Path.Combine(installDir, f.Path);
                Directory.CreateDirectory(Path.GetDirectoryName(dst)!);
                SaveAside(dst, f.Path);
                File.Move(Path.Combine(port, f.Path), dst);
            }
            foreach (var rel in remove)
            {
                var dst = Path.Combine(installDir, rel);
                if (!File.Exists(dst)) continue;
                var saved = Path.Combine(backup, rel);
                Directory.CreateDirectory(Path.GetDirectoryName(saved)!);
                File.Move(dst, saved);
                done.Add((dst, saved));
            }
            File.Delete(Path.Combine(installDir, Journal));
        }
        catch
        {
            RollBack(done);
            File.Delete(Path.Combine(installDir, Journal));
            throw;
        }
        finally
        {
            try { Directory.Delete(staging, true); } catch (IOException) { } catch (UnauthorizedAccessException) { }
        }
    }

    static void RollBack(List<(string Dst, string? Saved)> done)
    {
        for (var i = done.Count - 1; i >= 0; i--)
        {
            var (dst, saved) = done[i];
            try
            {
                if (saved is null) { if (File.Exists(dst)) File.Delete(dst); }  // a file the update added
                else if (File.Exists(saved)) File.Move(saved, dst, true);         // the file it replaced or removed
            }
            catch (Exception e) when (e is IOException or UnauthorizedAccessException) { }
        }
    }

    /// <summary>
    /// At the launcher's start: an update cut off half way (journal left) is put back from .update-backup; after a
    /// finished one the backup (with the old Launcher.exe) is removed. True when an interrupted update was rolled back.
    /// </summary>
    public static bool Tidy(string installDir)
    {
        var backup = Path.Combine(installDir, Backup);
        var journal = Path.Combine(installDir, Journal);
        var rolledBack = false;
        if (File.Exists(journal) && Directory.Exists(backup))
        {
            foreach (var saved in Directory.EnumerateFiles(backup, "*", SearchOption.AllDirectories))
            {
                var dst = Path.Combine(installDir, Path.GetRelativePath(backup, saved));
                try { File.Move(saved, dst, true); } catch (Exception e) when (e is IOException or UnauthorizedAccessException) { }
            }
            rolledBack = true;
        }
        try { File.Delete(journal); } catch (IOException) { }
        foreach (var dir in new[] { backup, Path.Combine(installDir, Staging) })
            try { if (Directory.Exists(dir)) Directory.Delete(dir, true); } catch (Exception e) when (e is IOException or UnauthorizedAccessException) { }
        return rolledBack;
    }
}

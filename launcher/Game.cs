using System.Security.Cryptography;
using MVPWorkshop.Core.Install;

namespace MVPSetup;

/// <summary>The game this setup installs, as the port's src/common/game_info.cpp and kImageSha1 name it.</summary>
static class Game
{
    public const string Title = "MVP 06 NCAA Baseball";
    public const string Product = "MVP 06 NCAA Baseball Recomp";
    public const string Exe = "MVP06.exe";
    public const string Xbe = "default.xbe";
    public const string XbeSha1 = "2f57040bab2c8b2eb33ebc7aa93209ad30dd103f";
    public const string UserFolder = "MVP06 Recomp";    // Documents\<this>: saves, settings, logs
    public const string UninstallKey = @"Software\Microsoft\Windows\CurrentVersion\Uninstall\MVP06Recomp";
    public const string GameFolder = "game";            // beside the exe; the runtime also accepts "assets"
    public const string SettingsFile = "mvp06-settings.toml";
    public const string Launcher = "Launcher.exe";      // setup's own exe, copied beside the game: launcher, repair, uninstall

    public const string InstallFolder = "MVP 06 Recomp";  // made inside the folder the player picks

    public static string DefaultInstallDir =>
        Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Programs", InstallFolder);

    /// <summary>The source as a game copy, or why it isn't one: an Xbox image or a folder holding default.xbe.</summary>
    public static IGameSource? OpenSource(string path, out string? error, out IDisposable? owner)
    {
        owner = null;
        error = null;
        IGameSource src;
        if (Directory.Exists(path))
        {
            if (!File.Exists(Path.Combine(path, Xbe))) { error = $"the folder has no {Xbe}"; return null; }
            src = new FolderSource(path);
        }
        else
        {
            var disc = XboxDisc.Open(path, out error);
            if (disc == null) return null;
            if (disc.Find(Xbe) == null) { error = $"the image has no {Xbe}"; disc.Dispose(); return null; }
            owner = disc;
            src = new DiscSource(disc);
        }
        var sha = Convert.ToHexStringLower(SHA1.HashData(src.Load(Xbe)));
        if (sha != XbeSha1)
        {
            error = $"this is another release of the game ({Xbe} SHA-1 {sha}); the port is built for the one with {XbeSha1}";
            owner?.Dispose();
            owner = null;
            return null;
        }
        return src;
    }
}

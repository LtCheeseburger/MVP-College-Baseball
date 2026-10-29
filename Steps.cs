using System.Diagnostics;
using Microsoft.Win32;

namespace MVPSetup;

/// <summary>The install steps besides the game data: port files, the game-code build, shortcuts, the uninstall entry.</summary>
static class Steps
{
    /// <summary>
    /// The port's own files beside setup (the release zip), or in a build folder with --port: the exe and its
    /// splash, the compiler without its work folder, the C++ runtime and the notices. Never game data.
    /// </summary>
    public static List<(string From, string Rel)> PortFiles(string portDir)
    {
        var list = new List<(string, string)>();
        void Add(string rel)
        {
            var p = Path.Combine(portDir, rel);
            if (File.Exists(p)) list.Add((p, rel));
        }
        Add(Game.Exe);
        // version.txt and port-manifest.json: what the launcher's updater compares with releases and swaps by
        foreach (var f in new[] { "splash.png", "splash.bmp", "README.md", "THIRD_PARTY.txt", "msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll", "version.txt", "port-manifest.json" }) Add(f);
        var compiler = Path.Combine(portDir, "compiler");
        if (Directory.Exists(compiler))
            foreach (var f in Directory.EnumerateFiles(compiler, "*", SearchOption.AllDirectories))
            {
                var rel = Path.GetRelativePath(portDir, f);
                if (!rel.StartsWith(Path.Combine("compiler", "work") + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase)) list.Add((f, rel));
            }
        return list;
    }

    public static void CopyPortFiles(string portDir, string installDir, IProgress<double>? progress)
    {
        var files = PortFiles(portDir);
        for (var i = 0; i < files.Count; i++)
        {
            var dst = Path.Combine(installDir, files[i].Rel);
            Directory.CreateDirectory(Path.GetDirectoryName(dst)!);
            File.Copy(files[i].From, dst, true);
            progress?.Report((i + 1.0) / files.Count);
        }
        // setup stays beside the game as its launcher, which also repairs and uninstalls
        var self = Environment.ProcessPath!;
        var target = Path.Combine(installDir, Game.Launcher);
        if (!string.Equals(Path.GetFullPath(self), Path.GetFullPath(target), StringComparison.OrdinalIgnoreCase)) File.Copy(self, target, true);
    }

    /// <summary>Runs the exe's own game-code build (its progress window); 0 start the game, 2 not now, 1 failed.</summary>
    public static int BuildGameCode(string installDir)
    {
        using var p = Process.Start(new ProcessStartInfo(Path.Combine(installDir, Game.Exe), "--build-game-code") { WorkingDirectory = installDir })!;
        p.WaitForExit();
        return p.ExitCode;
    }

    static string StartMenuLink => Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Programs), Game.Product + ".lnk");
    static string DesktopLink => Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory), Game.Product + ".lnk");

    public static void CreateShortcuts(string installDir, bool startMenu, bool desktop)
    {
        var launcher = Path.Combine(installDir, Game.Launcher);
        var icon = Path.Combine(installDir, Game.Exe);
        if (startMenu) Link(StartMenuLink, launcher, icon, installDir);
        if (desktop) Link(DesktopLink, launcher, icon, installDir);
    }

    static void Link(string lnk, string target, string icon, string dir)
    {
        var shell = Activator.CreateInstance(Type.GetTypeFromProgID("WScript.Shell")!)!;
        dynamic link = ((dynamic)shell).CreateShortcut(lnk);
        link.TargetPath = target;
        link.WorkingDirectory = dir;
        link.IconLocation = icon + ",0";  // the game's icon, not setup's
        link.Description = Game.Product;
        link.Save();
    }

    public static void RegisterUninstall(string installDir, long bytes)
    {
        using var key = Registry.CurrentUser.CreateSubKey(Game.UninstallKey);
        var setup = Path.Combine(installDir, Game.Launcher);
        key.SetValue("DisplayName", Game.Product);
        key.SetValue("DisplayVersion", typeof(Steps).Assembly.GetName().Version?.ToString(3) ?? "0.1.0");
        key.SetValue("DisplayIcon", Path.Combine(installDir, Game.Exe) + ",0");
        key.SetValue("Publisher", "MVP06 Recomp");
        key.SetValue("InstallLocation", installDir);
        key.SetValue("UninstallString", $"\"{setup}\" --uninstall");
        key.SetValue("ModifyPath", $"\"{setup}\" --repair");
        key.SetValue("EstimatedSize", (int)Math.Min(int.MaxValue, bytes / 1024), RegistryValueKind.DWord);
        key.SetValue("NoRepair", 1, RegistryValueKind.DWord);
    }

    /// <summary>The install this exe sits in (as Launcher.exe beside the game), else null.</summary>
    public static string? InstallDirOfThis()
    {
        var dir = Path.GetDirectoryName(Environment.ProcessPath!)!;
        return File.Exists(Path.Combine(dir, Game.Exe)) ? dir : null;
    }

    public static string? InstalledDir()
    {
        using var key = Registry.CurrentUser.OpenSubKey(Game.UninstallKey);
        return key?.GetValue("InstallLocation") as string;
    }

    public static string UserDir => Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments), Game.UserFolder);

    /// <summary>
    /// Removes the install folder, links and uninstall entry; saves, settings and logs in Documents only when asked.
    /// Setup can't delete the folder it runs from, so a copy in %TEMP% finishes the job.
    /// </summary>
    public static void Uninstall(string installDir, bool userData)
    {
        foreach (var l in new[] { StartMenuLink, DesktopLink })
            if (File.Exists(l)) File.Delete(l);
        Registry.CurrentUser.DeleteSubKeyTree(Game.UninstallKey, false);
        if (userData && Directory.Exists(UserDir)) Directory.Delete(UserDir, true);
        var self = Path.GetFullPath(Environment.ProcessPath!);
        if (self.StartsWith(Path.GetFullPath(installDir) + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase))
        {
            var temp = Path.Combine(Path.GetTempPath(), "MVP06-Setup-Uninstall.exe");
            File.Copy(self, temp, true);
            Process.Start(new ProcessStartInfo(temp, $"--remove-folder \"{installDir}\" --wait {Environment.ProcessId}") { UseShellExecute = false });
            return;
        }
        RemoveFolder(installDir);
    }

    public static void RemoveFolder(string dir)
    {
        for (var i = 0; i < 20 && Directory.Exists(dir); i++)
        {
            try { Directory.Delete(dir, true); }
            catch (IOException) { Thread.Sleep(500); }
            catch (UnauthorizedAccessException) { Thread.Sleep(500); }
        }
    }

    public static long FreeBytes(string dir)
    {
        var root = Path.GetPathRoot(Path.GetFullPath(dir))!;
        return new DriveInfo(root).AvailableFreeSpace;
    }
}

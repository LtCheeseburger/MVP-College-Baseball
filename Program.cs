using System.Diagnostics;
using System.Windows;

namespace MVPSetup;

// Setup.exe                 install; the port's files are in "Port Files" beside it, as in the release zip
// Launcher.exe              the same exe copied beside the installed game: the launcher
// Setup.exe --port <dir>    install with the port's files from a build folder (development)
// Setup.exe --repair        check the install against its manifest and restore from the disc image
// Setup.exe --uninstall     remove the install (Apps & features)
// Setup.exe --remove-folder <dir> --wait <pid>   the uninstaller's copy in %TEMP% deleting the install folder
static class Program
{
    public const string PortFolder = "Port Files";  // the release zip: Setup.exe and this folder

    [STAThread]
    static int Main(string[] args)
    {
        string? Arg(string name)
        {
            var i = Array.IndexOf(args, name);
            return i >= 0 && i + 1 < args.Length ? args[i + 1] : null;
        }
        if (Arg("--remove-folder") is { } folder)
        {
            if (int.TryParse(Arg("--wait"), out var pid))
                try { Process.GetProcessById(pid).WaitForExit(30000); } catch (ArgumentException) { }
            Steps.RemoveFolder(folder);
            return 0;
        }
        var mode = args.Contains("--uninstall") ? "uninstall" : args.Contains("--repair") ? "repair"
            : Steps.InstallDirOfThis() != null && Arg("--port") == null && !args.Contains("--quiet") ? "launcher" : "install";
        var portDir = Arg("--port") ?? Path.Combine(AppContext.BaseDirectory, PortFolder);
        if (mode == "install" && !File.Exists(Path.Combine(portDir, Game.Exe)))
        {
            MessageBox.Show($"The \"{PortFolder}\" folder with {Game.Exe} isn't beside setup. Unpack the whole release zip and run Setup.exe from there.", Game.Product);
            return 1;
        }
        // --quiet --source <image|folder> --dir <install dir> [--no-shortcuts] [--build]: the same steps, no window
        if (args.Contains("--quiet")) return Quiet(mode, portDir, Arg("--source"), Arg("--dir") ?? Steps.InstalledDir() ?? Game.DefaultInstallDir,
            !args.Contains("--no-shortcuts"), args.Contains("--build"), args.Contains("--delete-saves"));
        var app = new Application { ShutdownMode = ShutdownMode.OnMainWindowClose };
        if (mode == "launcher") return app.Run(new LauncherWindow(Steps.InstallDirOfThis()!));
        return app.Run(new SetupWindow(mode, portDir));
    }

    // exit codes: 0 done, 1 failed (reason in setup.log beside the install), 2 bad arguments
    static int Quiet(string mode, string portDir, string? source, string dir, bool shortcuts, bool build, bool deleteSaves)
    {
        var log = new List<string>();
        try
        {
            if (mode == "uninstall")
            {
                Steps.Uninstall(dir, deleteSaves);
                return 0;
            }
            if (source == null) return 2;
            var src = Game.OpenSource(source, out var why, out var owner) ?? throw new InvalidOperationException(why);
            using (owner)
            {
                var gameDir = Path.Combine(dir, Game.GameFolder);
                if (mode == "repair")
                {
                    var bad = MVPWorkshop.Core.Install.LooseInstaller.Verify(gameDir, null, CancellationToken.None);
                    new MVPWorkshop.Core.Install.LooseInstaller(src, gameDir).Restore(bad, null, CancellationToken.None);
                    var still = MVPWorkshop.Core.Install.LooseInstaller.Verify(gameDir, null, CancellationToken.None);
                    log.Add($"repair: {bad.Count} restored, {still.Count} still differ");
                    return still.Count == 0 ? 0 : 1;
                }
                Steps.CopyPortFiles(portDir, dir, null);
                new MVPWorkshop.Core.Install.LooseInstaller(src, gameDir).Install(new Dictionary<string, string>
                {
                    ["image"] = Path.GetFileName(source),
                    ["xbeSha1"] = Game.XbeSha1,
                    ["release"] = Game.Title,
                }, null, CancellationToken.None);
            }
            if (shortcuts) Steps.CreateShortcuts(dir, true, true);
            Steps.RegisterUninstall(dir, 2_710_000_000);
            if (build && Steps.BuildGameCode(dir) == 1) { log.Add("the game-code build failed"); return 1; }
            log.Add("installed");
            return 0;
        }
        catch (Exception e)
        {
            log.Add(e.ToString());
            return 1;
        }
        finally
        {
            if (mode != "uninstall")
                try { Directory.CreateDirectory(dir); File.AppendAllLines(Path.Combine(dir, "setup.log"), log.Prepend($"{DateTime.Now:s} {mode}")); }
                catch (IOException) { }
                catch (UnauthorizedAccessException) { }
        }
    }
}

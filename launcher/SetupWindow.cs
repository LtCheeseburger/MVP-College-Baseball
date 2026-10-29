using System.Diagnostics;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using Microsoft.Win32;
using MVPWorkshop.Core.Install;

namespace MVPSetup;

/// <summary>
/// Setup's one window: pick the folder, pick the game, install (copy the port, unpack the game), then shortcuts and launch;
/// with --repair it checks an install against its manifest and restores from the image; with --uninstall it removes it.
/// </summary>
sealed partial class SetupWindow : Window
{
    // measured on the full install (2026-10-28): the loose game data, and what the game-code build needs besides
    const long GameBytes = 2_710_000_000;
    const long BuildBytes = 450_000_000;

    readonly string mode;
    readonly string portDir;
    readonly StackPanel page = new() { Margin = new Thickness(24) };
    readonly CancellationTokenSource cancel = new();
    string isoPath = "";
    string installDir = "";

    public SetupWindow(string mode, string portDir)
    {
        this.mode = mode;
        this.portDir = portDir;
        Title = mode switch { "repair" => $"{Game.Product}: Repair", "uninstall" => $"{Game.Product}: Uninstall", _ => "MVP 27 College Baseball Setup" };
        WindowStartupLocation = WindowStartupLocation.CenterScreen;
        BuildShell();
        Closing += (_, _) => cancel.Cancel();
        switch (mode)
        {
            case "repair": installDir = Steps.InstallDirOfThis() ?? Steps.InstalledDir() ?? AppContext.BaseDirectory; ShowSource(); break;
            case "uninstall": ShowUninstall(); break;
            default: ShowFolder(); break;
        }
    }

    // ---- building blocks ----

    void Clear() { page.Children.Clear(); footer.Children.Clear(); }

    TextBlock Text(string s, bool heading = false)
    {
        var t = new TextBlock { Text = s, TextWrapping = TextWrapping.Wrap, Margin = new Thickness(0, 0, 0, 12) };
        if (heading) { t.Text = s.ToUpperInvariant(); t.FontSize = 40; t.FontFamily = displayFont; t.FontWeight = FontWeights.Bold; }
        else { t.Foreground = Ink("#A6B2BD"); t.LineHeight = 27; }
        page.Children.Add(t);
        return t;
    }

    Button Buttons(string primary, Action onPrimary, string? secondary = null, Action? onSecondary = null)
    {
        var row = new Grid { Margin = new Thickness(0, 24, 0, 0) };
        if (secondary != null)
        {
            var b2 = new Button { Content = secondary.ToUpperInvariant(), MinWidth = 140, Padding = new Thickness(20, 12, 20, 12), FontFamily = displayFont, FontSize = 23, HorizontalAlignment = HorizontalAlignment.Left };
            b2.Click += (_, _) => onSecondary!();
            row.Children.Add(b2);
        }
        var b = new Button { Content = primary.ToUpperInvariant(), MinWidth = 220, Padding = new Thickness(20, 12, 20, 12), IsDefault = true, HorizontalAlignment = HorizontalAlignment.Right, Background = Ink("#C60020"), BorderBrush = Ink("#FF173E"), FontFamily = displayFont, FontWeight = FontWeights.Bold, FontSize = 26 };
        b.Click += (_, _) => onPrimary();
        row.Children.Add(b);
        footer.Children.Add(new Border { BorderBrush = Ink("#34424B"), BorderThickness = new Thickness(0,1,0,0), Child = row });
        return b;
    }

    (TextBox, Button) PathRow(string value)
    {
        var grid = new DockPanel { Margin = new Thickness(0, 0, 0, 8) };
        var browse = new Button { Content = "Browse…", Padding = new Thickness(12, 2, 12, 2), Margin = new Thickness(8, 0, 0, 0) };
        DockPanel.SetDock(browse, Dock.Right);
        var box = new TextBox { Text = value, Padding = new Thickness(12), FontSize = 18, Background = Ink("#0C141A"), Foreground = Brushes.White, CaretBrush = Brushes.White, BorderBrush = Ink("#55616B") };
        grid.Children.Add(browse);
        grid.Children.Add(box);
        page.Children.Add(grid);
        return (box, browse);
    }

    TextBlock Error()
    {
        var t = Text("");
        t.Foreground = Ink("#FF7187");
        t.Visibility = Visibility.Collapsed;
        return t;
    }

    static void Show(TextBlock t, string? message)
    {
        t.Text = message ?? "";
        t.Visibility = string.IsNullOrEmpty(message) ? Visibility.Collapsed : Visibility.Visible;
    }

    // ---- 1. the folder ----

    void ShowFolder()
    {
        Clear();
        SetStep(0);
        Text("Install location", true);
        Text($"Pick where the game goes. It is installed for your Windows account only (no administrator rights needed); saves and settings stay in Documents\\{Game.UserFolder}.");
        var (box, browse) = PathRow(installDir);
        browse.Click += (_, _) =>
        {
            var dlg = new OpenFolderDialog { Title = $"Install folder (a \"{Game.InstallFolder}\" folder is made in it)" };
            if (dlg.ShowDialog(this) != true) return;
            // the game gets its own folder inside the one picked, unless that already is it
            var picked = Path.TrimEndingDirectorySeparator(dlg.FolderName);
            box.Text = Path.GetFileName(picked).Equals(Game.InstallFolder, StringComparison.OrdinalIgnoreCase) ? picked : Path.Combine(picked, Game.InstallFolder);
        };
        var need = NeededBytes();
        var space = new TextBlock { Foreground = Ink("#B2BDC6"), FontSize = 18, LineHeight = 27 };
        page.Children.Add(new Border { Child = space, Padding = new Thickness(20,14,20,14), Margin = new Thickness(0,24,0,12), BorderBrush = Ink("#303D45"), BorderThickness = new Thickness(1), CornerRadius = new CornerRadius(3) });
        Button? next = null;
        void Measure()
        {
            bool valid = false;
            var dir = box.Text.Trim().Trim('"');
            space.Text = $"Space Required: {need / 1e9:F1} GB\nSpace Available: Choose an install folder";
            try
            {
                if (Path.IsPathFullyQualified(dir))
                {
                    long free = Steps.FreeBytes(dir);
                    space.Text = $"Space Required: {need / 1e9:F1} GB\nSpace Available: {free / 1e9:F1} GB";
                    valid = free >= need;
                }
            }
            catch { }
            if (next != null) next.IsEnabled = valid;
        }
        box.TextChanged += (_, _) => Measure();
        Measure();
        var error = Error();
        next = Buttons("Next", () =>
        {
            var dir = box.Text.Trim().Trim('"');
            try
            {
                if (!Path.IsPathFullyQualified(dir)) { Show(error, "Pick a full folder path."); return; }
                if (Steps.FreeBytes(dir) < need) { Show(error, "There isn't enough free space on that drive."); return; }
                if (File.Exists(Path.Combine(dir, Game.GameFolder, LooseInstaller.ManifestName)) &&
                    MessageBox.Show(this, "The game is already installed there. Install over it?", Title, MessageBoxButton.YesNo) != MessageBoxResult.Yes) return;
            }
            catch (Exception e) { Show(error, e.Message); return; }
            installDir = dir;
            ShowSource();
        }, "Cancel", Close);
        Measure();
    }

    long NeededBytes() => GameBytes + BuildBytes + Steps.PortFiles(portDir).Sum(f => new FileInfo(f.From).Length);

    // ---- 2. the game ----

    void ShowSource()
    {
        Clear();
        Text(mode == "repair" ? "Repair" : "Your game", true);
        Text(mode == "repair"
            ? "Setup checks every game file against the list made at install and restores anything missing or changed from your game. Saves are not touched. Pick the disc image or extracted folder you installed from."
            : $"Pick your own copy of {Game.Title} for the original Xbox: its disc image (.iso, a full dump or an xiso), or a folder of its extracted files (the one holding {Game.Xbe}). Nothing of the game comes with this setup.");
        var (box, browse) = PathRow(isoPath);
        browse.Click += (_, _) =>
        {
            var dlg = new OpenFileDialog { Filter = "Xbox disc image (*.iso)|*.iso|Extracted game (default.xbe)|default.xbe|All files|*.*", Title = "The game's disc image, or default.xbe in its extracted folder" };
            if (dlg.ShowDialog(this) != true) return;
            // default.xbe stands for the folder around it
            box.Text = string.Equals(Path.GetFileName(dlg.FileName), Game.Xbe, StringComparison.OrdinalIgnoreCase) ? Path.GetDirectoryName(dlg.FileName)! : dlg.FileName;
        };
        var error = Error();
        Buttons(mode == "repair" ? "Repair" : "Install", () =>
        {
            Show(error, null);
            var path = box.Text.Trim().Trim('"');
            var src = Game.OpenSource(path, out var why, out var owner);
            if (src == null) { Show(error, $"This can't be used: {why}."); return; }
            owner?.Dispose();
            isoPath = path;
            if (mode == "repair") ShowRepair(); else _ = RunInstall();
        }, mode == "repair" ? "Cancel" : "Back", mode == "repair" ? Close : ShowFolder);
    }

    // ---- 3. installing ----

    (TextBlock, ProgressBar) ProgressPage(string heading)
    {
        Clear();
        SetStep(1);
        Text(heading, true);
        var label = Text("");
        var bar = new ProgressBar { Foreground = Ink("#E60024"), Background = Ink("#18232B"), Height = 18, Maximum = 1, Margin = new Thickness(0, 0, 0, 8) };
        page.Children.Add(bar);
        Buttons("Cancel", () => cancel.Cancel());
        return (label, bar);
    }

    async Task RunInstall()
    {
        var (label, bar) = ProgressPage("Installing");
        try
        {
            label.Text = "Copying the port's files…";
            var copy = new Progress<double>(v => bar.Value = v);
            await Task.Run(() => Steps.CopyPortFiles(portDir, installDir, copy));

            label.Text = "Unpacking the game into loose files and converting its textures to DDS. This takes a few minutes; a cancelled install carries on where it stopped.";
            bar.Value = 0;
            var gameDir = Path.Combine(installDir, Game.GameFolder);
            var p = new Progress<LooseInstaller.Progress>(x => bar.Value = (double)x.Done / Math.Max(1, x.Total));
            await Task.Run(() =>
            {
                var src = Game.OpenSource(isoPath, out var why, out var owner) ?? throw new InvalidOperationException(why);
                using (owner)
                    new LooseInstaller(src, gameDir).Install(new Dictionary<string, string>
                    {
                        ["image"] = Path.GetFileName(isoPath.TrimEnd('\\', '/')),
                        ["xbeSha1"] = Game.XbeSha1,
                        ["release"] = Game.Title,
                        ["setup"] = typeof(SetupWindow).Assembly.GetName().Version?.ToString(3) ?? "",
                    }, p, cancel.Token);
            });
            Steps.RegisterUninstall(installDir, GameBytes + Steps.PortFiles(portDir).Sum(f => new FileInfo(f.From).Length));
            ShowFinish();
        }
        catch (OperationCanceledException)
        {
            ShowStopped("Setup was cancelled. Run it again with the same folder to carry on where it stopped.");
        }
        catch (Exception e)
        {
            ShowStopped($"Setup stopped: {e.Message}");
        }
    }

    // ---- 4. finish ----

    void ShowFinish()
    {
        Clear();
        SetStep(2);
        Text("Complete", true);
        Text($"{Game.Product} is installed in {installDir}. The first start builds the game's code for this PC, once; that takes a while.");
        CheckBox Box(string text)
        {
            var c = new CheckBox { Content = text, IsChecked = true, Margin = new Thickness(0, 4, 0, 4) };
            page.Children.Add(c);
            return c;
        }
        var startMenu = Box("Start menu shortcut");
        var desktop = Box("Desktop shortcut");
        var launch = Box("Open the launcher");
        var error = Error();
        Buttons("Finish", () =>
        {
            try { Steps.CreateShortcuts(installDir, startMenu.IsChecked == true, desktop.IsChecked == true); }
            catch (Exception e) { Show(error, $"The shortcuts couldn't be made: {e.Message}"); return; }
            if (launch.IsChecked == true) Process.Start(new ProcessStartInfo(Path.Combine(installDir, Game.Launcher)) { WorkingDirectory = installDir });
            Close();
        });
    }

    void ShowStopped(string message)
    {
        Clear();
        Text("Not finished", true);
        Text(message);
        Buttons("Close", Close);
    }

    // ---- repair ----

    async void ShowRepair()
    {
        var (label, bar) = ProgressPage("Repair");
        var gameDir = Path.Combine(installDir, Game.GameFolder);
        try
        {
            if (!File.Exists(Path.Combine(gameDir, LooseInstaller.ManifestName)))
            {
                ShowStopped($"No install was found in {installDir} (its game\\{LooseInstaller.ManifestName} is missing). Run setup to install instead.");
                return;
            }
            label.Text = "Checking every game file…";
            var p = new Progress<LooseInstaller.Progress>(x => bar.Value = (double)x.Done / Math.Max(1, x.Total));
            var bad = await Task.Run(() => LooseInstaller.Verify(gameDir, p, cancel.Token));
            if (bad.Count == 0)
            {
                Clear();
                Text("Nothing to repair", true);
                Text("Every game file matches the install.");
                Buttons("Close", Close);
                return;
            }
            label.Text = $"Restoring {bad.Count} missing or changed files from the disc image…";
            bar.Value = 0;
            var r = new Progress<LooseInstaller.Progress>(x => bar.Value = (double)x.Done / Math.Max(1, x.Total));
            await Task.Run(() =>
            {
                var src = Game.OpenSource(isoPath, out var why, out var owner) ?? throw new InvalidOperationException(why);
                using (owner) new LooseInstaller(src, gameDir).Restore(bad, r, cancel.Token);
            });
            var still = await Task.Run(() => LooseInstaller.Verify(gameDir, null, cancel.Token));
            Clear();
            Text(still.Count == 0 ? "Repaired" : "Not everything could be restored", true);
            Text(still.Count == 0
                ? $"{bad.Count} files were restored."
                : $"{still.Count} files still differ, for example {still[0].Path}.");
            Buttons("Close", Close);
        }
        catch (OperationCanceledException) { ShowStopped("Repair was cancelled."); }
        catch (Exception e) { ShowStopped($"Repair stopped: {e.Message}"); }
    }

    // ---- uninstall ----

    void ShowUninstall()
    {
        installDir = Steps.InstallDirOfThis() ?? Steps.InstalledDir() ?? AppContext.BaseDirectory.TrimEnd(Path.DirectorySeparatorChar);
        Clear();
        Text($"Uninstall {Game.Product}", true);
        Text($"This removes {installDir}, its shortcuts and its entry in Apps & features.");
        var user = new CheckBox { Content = $"Also delete saves, settings and logs (Documents\\{Game.UserFolder})", Margin = new Thickness(0, 4, 0, 8) };
        page.Children.Add(user);
        Buttons("Uninstall", () =>
        {
            if (user.IsChecked == true &&
                MessageBox.Show(this, "Your saves will be deleted for good. Continue?", Title, MessageBoxButton.YesNo, MessageBoxImage.Warning) != MessageBoxResult.Yes) return;
            if (Process.GetProcessesByName(Path.GetFileNameWithoutExtension(Game.Exe)).Any(p => IsInside(p, installDir)))
            {
                MessageBox.Show(this, "Close the game first.", Title);
                return;
            }
            Steps.Uninstall(installDir, user.IsChecked == true);
            Clear();
            Text("Uninstalled", true);
            Text(user.IsChecked == true ? "The game and your saves were removed." : $"The game was removed. Your saves are still in Documents\\{Game.UserFolder}.");
            Buttons("Close", Close);
        }, "Cancel", Close);
    }

    static bool IsInside(Process p, string dir)
    {
        try { return p.MainModule?.FileName?.StartsWith(dir, StringComparison.OrdinalIgnoreCase) == true; }
        catch { return false; }
    }
}

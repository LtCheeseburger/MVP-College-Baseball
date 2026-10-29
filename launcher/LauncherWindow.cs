using System.Diagnostics;
using System.Security.Cryptography;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using MVPWorkshop.Core.Install;

namespace MVPSetup;

/// <summary>
/// The launcher (Launcher.exe beside the installed game, where the shortcuts point): checks the install, starts the
/// game, edits its settings file and restores saves from the runtime's backups.
/// </summary>
sealed partial class LauncherWindow : Window
{
    readonly string installDir;
    readonly UserData user;
    readonly TabControl tabs = new() { Margin = new Thickness(12) };

    public LauncherWindow(string installDir)
    {
        this.installDir = installDir;
        user = new UserData(installDir);
        Title = Game.Product;
        WindowStartupLocation = WindowStartupLocation.CenterScreen;
        BuildShell();
        SetUpUpdates();
        tabs.Items.Add(new TabItem { Header = "HOME", Content = HomeTab() });
        tabs.Items.Add(new TabItem { Header = "MODS", Content = ModsTab() });
        tabs.Items.Add(new TabItem { Header = "SETTINGS", Content = SettingsTab() });
    }

    // ---- Mods (docs/MVPMOD.md: the launcher writes mods\mods.json, the game reads it at its next start) ----

    UIElement ModsTab()
    {
        var mods = new ModList(installDir);
        var p = new DockPanel { Margin = new Thickness(30) };
        var head = new StackPanel();
        head.Children.Add(Heading("MODS"));
        head.Children.Add(Line("Turned-on mods are used from the game's next start. When two mods change the same file, the one lower in the list wins. Mods never change the installed game files: turn a mod off and the game is as it was.", 13));
        DockPanel.SetDock(head, Dock.Top);
        p.Children.Add(head);

        var order = new ListBox { Margin = new Thickness(0,0,0,12) };
        var search = new TextBox { Margin = new Thickness(0,4,0,12), ToolTip = "Search installed mods" };
        head.Children.Add(Line("Search installed mods", 13));
        head.Children.Add(search);
        var list = new ListBox { Margin = new Thickness(0, 0, 0, 8), MinHeight = 140 };
        var details = new TextBlock { TextWrapping = TextWrapping.Wrap, FontSize = 13, Margin = new Thickness(0, 0, 0, 8) };
        var problems = new TextBlock { TextWrapping = TextWrapping.Wrap, FontSize = 13, Foreground = Ink("#FF7C8D"), Margin = new Thickness(0, 0, 0, 8) };

        void Changed()
        {
            try { mods.Save(); }
            catch (Exception e) when (e is IOException or UnauthorizedAccessException) { MessageBox.Show(this, "Couldn't save the mod list: " + e.Message, Title); }
            Fill();
        }

        void ShowDetails()
        {
            if (list.SelectedItem is not ListBoxItem { Tag: ModList.Mod m }) { details.Text = ""; return; }
            var t = $"{m.Name} {m.Version}" + (m.Author != "" ? $" by {m.Author}" : "") + $"    ({m.FileName})";
            if (m.Error != null) t += $"\nCan't be used: {m.Error}.";
            else
            {
                if (m.Description != "") t += "\n" + m.Description;
                t += $"\nChanges {m.Files.Count} game file{(m.Files.Count == 1 ? "" : "s")}.";
                if (m.Requires.Count > 0) t += "\nNeeds: " + string.Join(", ", m.Requires.Select(r => r.Id + (r.Version != "" ? " " + r.Version : "")));
                if (m.AffectsSaves) t += "\nChanges game data that saves depend on: a dynasty started with it may not load the same way without it.";
                var lost = mods.Overlaps().Where(o => o.Losers.Contains(m)).ToList();
                var won = mods.Overlaps().Where(o => o.Winner == m).ToList();
                if (won.Count > 0) t += $"\nOverrides {won.Count} file{(won.Count == 1 ? "" : "s")} of " + string.Join(", ", won.SelectMany(o => o.Losers).Distinct().Select(x => x.Name)) + ".";
                if (lost.Count > 0) t += $"\n{lost.Count} of its file{(lost.Count == 1 ? " is" : "s are")} overridden by " + string.Join(", ", lost.Select(o => o.Winner).Distinct().Select(x => x.Name)) + ".";
            }
            details.Text = t;
        }

        void Fill()
        {
            var keep = (list.SelectedItem as ListBoxItem)?.Tag;
            list.Items.Clear();
            order.Items.Clear();
            var overlaps = mods.Overlaps();
            foreach (var m in mods.Mods)
            {
                var orderRow = new ListBoxItem { Content = $"{mods.Mods.IndexOf(m) + 1:00}   {(m.Enabled ? "●" : "○")}   {m.Name}", Tag = m, Padding = new Thickness(12,14,12,14) };
                order.Items.Add(orderRow);
                if (m == keep) order.SelectedItem = orderRow;
                if (!m.Name.Contains(search.Text, StringComparison.OrdinalIgnoreCase) && !m.Description.Contains(search.Text, StringComparison.OrdinalIgnoreCase)) continue;
                var row = new DockPanel { Margin = new Thickness(2) };
                var box = new CheckBox { IsChecked = m.Enabled, IsEnabled = m.Error == null, VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(0, 0, 8, 0) };
                box.Checked += (_, _) => { m.Enabled = true; Changed(); };
                box.Unchecked += (_, _) => { m.Enabled = false; Changed(); };
                row.Children.Add(box);
                row.Children.Add(ModIcon(m));
                var note = m.Error != null ? "can't be used" : !m.Enabled ? "off" : overlaps.Any(o => o.Losers.Contains(m)) ? "partly overridden" : "";
                var status = new TextBlock { Text = note, Foreground = m.Error != null ? Ink("#FF7C8D") : Ink("#A2B2BF"), Margin = new Thickness(12, 0, 0, 0) };
                DockPanel.SetDock(status, Dock.Right);
                row.Children.Add(status);
                var name = new StackPanel { VerticalAlignment = VerticalAlignment.Center };
                name.Children.Add(new TextBlock { Text = m.Name, Foreground = Ink("#EDF0F3"), FontSize = 16, TextTrimming = TextTrimming.CharacterEllipsis });
                name.Children.Add(new TextBlock { Text = $"v{m.Version}" + (m.Author != "" ? $"  |  {m.Author}" : ""), Foreground = Ink("#A2B2BF"), FontSize = 12 });
                row.Children.Add(name);
                var item = new ListBoxItem { Content = row, Tag = m };
                list.Items.Add(item);
                if (m == keep) list.SelectedItem = item;
            }
            if (mods.Mods.Count == 0) list.Items.Add(new ListBoxItem { Content = "No mods yet. Add a .mvpmod file, or put one in the mods folder.", IsEnabled = false });
            var probs = mods.Problems().Distinct().ToList();
            problems.Text = string.Join("\n", probs);
            problems.Visibility = probs.Count > 0 ? Visibility.Visible : Visibility.Collapsed;
            ShowDetails();
        }
        list.SelectionChanged += (_, _) => ShowDetails();
        search.TextChanged += (_, _) => Fill();
        order.SelectionChanged += (_, _) =>
        {
            if (order.SelectedItem is not ListBoxItem { Tag: ModList.Mod selected }) return;
            var item = list.Items.OfType<ListBoxItem>().FirstOrDefault(x => x.Tag == selected);
            if (item != null) { list.SelectedItem = item; list.ScrollIntoView(item); }
        };

        void Move(int by)
        {
            if (list.SelectedItem is not ListBoxItem { Tag: ModList.Mod m }) return;
            var i = mods.Mods.IndexOf(m);
            var j = i + by;
            if (j < 0 || j >= mods.Mods.Count) return;
            (mods.Mods[i], mods.Mods[j]) = (mods.Mods[j], mods.Mods[i]);
            Changed();
        }

        void AddMod()
        {
            var dlg = new Microsoft.Win32.OpenFileDialog { Filter = "MVP mods (*.mvpmod)|*.mvpmod", Multiselect = true, Title = "Add mods" };
            if (dlg.ShowDialog(this) != true) return;
            foreach (var f in dlg.FileNames)
            {
                try
                {
                    var m = mods.Add(f);
                    if (m.Error != null) MessageBox.Show(this, $"{Path.GetFileName(f)} wasn't added: {m.Error}.", Title);
                }
                catch (Exception e) when (e is IOException or UnauthorizedAccessException) { MessageBox.Show(this, $"{Path.GetFileName(f)} wasn't added: {e.Message}", Title); }
            }
            mods.Reload();
            Changed();
        }

        void RemoveMod()
        {
            if (list.SelectedItem is not ListBoxItem { Tag: ModList.Mod m }) { MessageBox.Show(this, "Pick a mod in the list first.", Title); return; }
            if (MessageBox.Show(this, $"Delete {m.FileName} from the mods folder?", Title, MessageBoxButton.YesNo, MessageBoxImage.Question) != MessageBoxResult.Yes) return;
            try
            {
                if (Directory.Exists(m.Path)) Directory.Delete(m.Path, true);
                else File.Delete(m.Path);
            }
            catch (Exception e) when (e is IOException or UnauthorizedAccessException) { MessageBox.Show(this, "Couldn't delete it: " + e.Message, Title); }
            mods.Reload();
            Changed();
        }

        var buttons = new StackPanel();
        buttons.Children.Add(problems);

        buttons.Children.Add(Row(Btn("Add mod…", AddMod), Btn("Remove", RemoveMod), Btn("Move up", () => Move(-1)), Btn("Move down", () => Move(1))));
        buttons.Children.Add(Row(Btn("Refresh", () => { mods.Reload(); Fill(); }), Btn("Open mods folder", () => Open(mods.Dir)), Btn("Play without mods", () => Start("--no-mods"))));
        DockPanel.SetDock(buttons, Dock.Bottom);
        p.Children.Add(buttons);
        var columns = new Grid();
        columns.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1.25, GridUnitType.Star) });
        columns.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(28) });
        columns.ColumnDefinitions.Add(new ColumnDefinition());
        columns.Children.Add(list);
        var side = new DockPanel(); Grid.SetColumn(side, 2); columns.Children.Add(side);
        var orderHead = new StackPanel(); orderHead.Children.Add(Heading("LOAD ORDER")); orderHead.Children.Add(Line("Lower mods take priority. Select a mod and use Move up / down.", 13));
        DockPanel.SetDock(orderHead, Dock.Top); side.Children.Add(orderHead);
        var info = Panel(details); DockPanel.SetDock(info, Dock.Bottom); side.Children.Add(info);
        side.Children.Add(order);
        p.Children.Add(columns);
        Fill();
        return p;
    }

    string Exe => Path.Combine(installDir, Game.Exe);

    bool GameRunning() => Process.GetProcessesByName(Path.GetFileNameWithoutExtension(Game.Exe)).Any(p =>
    {
        try { return string.Equals(p.MainModule?.FileName, Exe, StringComparison.OrdinalIgnoreCase); }
        catch { return false; }
    });

    void Start(string args)
    {
        if (GameRunning()) { MessageBox.Show(this, "The game is already running.", Title); return; }
        Process.Start(new ProcessStartInfo(Exe, args) { WorkingDirectory = installDir });
        Close();
    }

    static TextBlock Line(string text, double size = 14, bool bold = false) =>
        new() { Text = text, FontSize = size, FontWeight = bold ? FontWeights.SemiBold : FontWeights.Normal, TextWrapping = TextWrapping.Wrap, Margin = new Thickness(0, 0, 0, 8) };

    static Button Btn(string text, Action click, bool primary = false)
    {
        var b = new Button { Content = text, Padding = new Thickness(16, 6, 16, 6), Margin = new Thickness(0, 0, 8, 8), MinWidth = 110 };
        if (primary) { b.Background = Ink("#C90023"); b.BorderBrush = Ink("#FF173B"); b.FontSize = 18; b.Padding = new Thickness(32, 10, 32, 10); b.IsDefault = true; }
        b.Click += (_, _) => click();
        return b;
    }

    static WrapPanel Row(params UIElement[] items)
    {
        var p = new WrapPanel { Margin = new Thickness(0, 4, 0, 4) };
        foreach (var i in items) p.Children.Add(i);
        return p;
    }

    static void Open(string dir)
    {
        Directory.CreateDirectory(dir);
        Process.Start(new ProcessStartInfo("explorer.exe", $"\"{dir}\""));
    }

    // ---- Play ----

    UIElement PlayTab()
    {
        var p = new StackPanel { Margin = new Thickness(16) };
        p.Children.Add(Line("Game files", 16, true));
        var verifyLabel = Line("Checks every game file against the list made at install. A file you changed on purpose shows up as changed too.");
        var bar = new ProgressBar { Height = 14, Maximum = 1, Margin = new Thickness(0, 0, 0, 8), Visibility = Visibility.Collapsed };
        p.Children.Add(verifyLabel);
        p.Children.Add(bar);
        Button? verify = null;
        verify = Btn("Verify files", async () =>
        {
            var gameDir = Path.Combine(installDir, Game.GameFolder);
            if (!File.Exists(Path.Combine(gameDir, LooseInstaller.ManifestName))) { verifyLabel.Text = "This install has no file list (manifest.json); run setup to install it again."; return; }
            verify!.IsEnabled = false;
            bar.Visibility = Visibility.Visible;
            var progress = new Progress<LooseInstaller.Progress>(x => bar.Value = (double)x.Done / Math.Max(1, x.Total));
            var bad = await Task.Run(() => LooseInstaller.Verify(gameDir, progress, CancellationToken.None));
            bar.Visibility = Visibility.Collapsed;
            verify.IsEnabled = true;
            verifyLabel.Text = bad.Count == 0 ? "Every game file matches the install." : $"{bad.Count} files are missing or changed, for example {bad[0].Path}. Repair restores them from your disc image.";
        });
        p.Children.Add(Row(verify, Btn("Repair…", Repair)));

        p.Children.Add(Line("Updates", 16, true));
        p.Children.Add(Line($"Version {InstalledVersion}. The launcher checks for a newer release each time it opens.", 13));
        p.Children.Add(Row(Btn("Check for updates", () => _ = CheckForUpdates(quiet: false))));

        p.Children.Add(Line("Folders", 16, true));
        p.Children.Add(Row(Btn("Install folder", () => Open(installDir)), Btn("Saves and settings", () => Open(user.Dir)), Btn("Logs", () => Open(Path.Combine(user.Dir, "logs")))));
        return new ScrollViewer { Content = p, VerticalScrollBarVisibility = ScrollBarVisibility.Auto };
    }

    void Repair()
    {
        if (GameRunning()) { MessageBox.Show(this, "Close the game first.", Title); return; }
        Process.Start(new ProcessStartInfo(Environment.ProcessPath!, "--repair") { WorkingDirectory = installDir });
        Close();
    }

    /// <summary>A quick check: the exe, the game's default.xbe and its release. Verify reads every file.</summary>
    bool Check(out string problem, out string note)
    {
        problem = note = "";
        if (!File.Exists(Exe)) { problem = $"{Game.Exe} is missing from {installDir}. Repair or install again."; return false; }
        var xbe = new[] { Game.GameFolder, "assets" }.Select(d => Path.Combine(installDir, d, Game.Xbe)).FirstOrDefault(File.Exists);
        if (xbe == null) { problem = $"The game files are missing ({Game.GameFolder}\\{Game.Xbe}). Repair restores them from your disc image."; return false; }
        if (Convert.ToHexStringLower(SHA1.HashData(File.ReadAllBytes(xbe))) != Game.XbeSha1) { problem = $"{Game.Xbe} is from another release or changed. Repair restores it."; return false; }
        note = File.Exists(Path.Combine(installDir, Path.ChangeExtension(Game.Exe, ".dll")))
            ? "Ready to play."
            : "Ready. The first start builds the game's code for this PC, once; that takes a while.";
        return true;
    }

    // ---- Settings ----

    // the runtime's keys (sdk/runtime/app/settings.cpp and src/common/game_info.cpp) with its defaults
    sealed record Field(string Key, string Label, string Default, string[]? Choices = null, int Min = 0, int Max = 0, bool Bool = false, string Help = "");

    static readonly (string Group, Field[] Fields)[] Fields =
    [
        ("Display", [
            new("display_mode", "Window", "borderless", ["borderless", "fullscreen", "windowed"]),
            new("width", "Width (0 = desktop)", "0", Min: 0, Max: 7680),
            new("height", "Height (0 = desktop)", "0", Min: 0, Max: 4320),
            new("xbox_720p", "Widescreen 720p mode", "true", Bool: true, Help: "The game's own 720p layout"),
            new("frame_aspect", "Frame aspect x100 (0 = the display's)", "0", Min: 0, Max: 400, Help: "Wider than 177 is ultrawide: 237 = 21:9, 356 = 32:9"),
            new("ultrawide_menus", "Ultrawide menus", "false", Bool: true, Help: "Off: the menus stay 16:9, pillarboxed"),
            new("internal_resolution", "Internal resolution", "native", ["native", "720p", "1080p", "1440p"]),
            new("render_percent", "Render scale % (native)", "100", Min: 25, Max: 200),
            new("frame_limit", "Frame limit (0 = off)", "60", Min: 0, Max: 1000),
            new("vsync", "VSync", "true", Bool: true),
            new("background_fps", "Frame limit in the background", "30", Min: 0, Max: 1000),
        ]),
        ("Graphics", [
            new("antialiasing", "Antialiasing", "fxaa", ["fxaa", "off"]),
            new("upscaler", "Upscaler", "bilinear", ["bilinear", "fsr1"]),
            new("sharpening", "Sharpening (0-100)", "0", Min: 0, Max: 100),
            new("anisotropic_filtering", "Anisotropic filtering (0 = game)", "0", ["0", "2", "4", "8", "16"]),
            new("effects_quality", "Effects quality", "high", ["high", "original"]),
            new("depth_of_field", "Depth of field", "true", Bool: true),
            new("glow", "Glow", "true", Bool: true),
            new("frame_stats", "Frame stats", "off", ["off", "log", "overlay", "both"]),
        ]),
        ("Sound and controls", [
            new("mute_unfocused", "Mute in the background", "true", Bool: true),
            new("walkup_volume", "Walk-up songs volume % (0 off)", "100", Min: 0, Max: 200, Help: "Songs from the walkup folder beside MVP06.exe"),
            new("rumble", "Rumble %", "100", Min: 0, Max: 100),
            new("deadzone_left", "Left stick deadzone %", "0", Min: 0, Max: 50),
            new("deadzone_right", "Right stick deadzone %", "0", Min: 0, Max: 50),
            new("nintendo_buttons", "Nintendo controller buttons", "label", ["label", "position"]),
            new("screenshot_key", "Screenshot key (virtual key code, 123 = F12, 0 off)", "123", Min: 0, Max: 254),
        ]),
        ("Save backups", [
            new("save_backups", "Backups of all saves kept (0 off)", "10", Min: 0, Max: 100),
            new("save_history", "Earlier versions kept per save", "5", Min: 2, Max: 20),
        ]),
        ("Textures and network", [
            new("custom_textures", "Replacement textures (textures\\replacement)", "false", Bool: true),
            new("dump_textures", "Dump textures", "false", Bool: true),
            new("texture_dump_format", "Dump format", "0", ["0", "1"], Help: "0 PNG, 1 DDS"),
            new("join_ip", "Co-op: host address beyond the LAN", ""),
        ]),
    ];

    UIElement SettingsTab()
    {
        var p = new StackPanel { Margin = new Thickness(16) };
        p.Children.Add(Line("Changes take effect the next time the game starts. The game also saves these when you change them in its PC Options menu, so change them here while it's closed.", 13));
        var file = new SettingsFile(user.SettingsPath);
        var setters = new List<Action>();
        var shell = new DockPanel { Margin = new Thickness(30,8,30,20) };
        var categories = new StackPanel { Width = 210, Margin = new Thickness(0,0,28,0) };
        DockPanel.SetDock(categories, Dock.Left); shell.Children.Add(categories);
        var cards = new List<UIElement>();
        var categoryButtons = new List<Button>();
        foreach (var (group, fields) in Fields)
        {
            var section = new StackPanel(); section.Children.Add(Heading(group.ToUpperInvariant()));
            var card = Panel(section); card.Visibility = cards.Count == 0 ? Visibility.Visible : Visibility.Collapsed;
            cards.Add(card); p.Children.Add(card);
            Button? category = null;
            category = Btn(group, () => { foreach (var c in cards) c.Visibility = c == card ? Visibility.Visible : Visibility.Collapsed; foreach (var b in categoryButtons) { b.Background = Ink(b == category ? "#52101D" : "#131D24"); b.BorderBrush = Ink(b == category ? "#ED0026" : "#40515D"); } });
            if (categoryButtons.Count == 0) { category.Background = Ink("#52101D"); category.BorderBrush = Ink("#ED0026"); }
        categoryButtons.Add(category);
            category.HorizontalAlignment = HorizontalAlignment.Stretch; category.Padding = new Thickness(12,16,12,16);
            categories.Children.Add(category);
            var grid = new Grid { Margin = new Thickness(0, 0, 0, 12) };
            grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(300) });
            grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
            foreach (var f in fields)
            {
                grid.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
                var row = grid.RowDefinitions.Count - 1;
                var label = new TextBlock { Text = f.Label, VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(0, 4, 8, 4), ToolTip = f.Help.Length > 0 ? f.Help : f.Key };
                Grid.SetRow(label, row);
                grid.Children.Add(label);
                var current = file.Get(f.Key) ?? f.Default;
                FrameworkElement editor;
                if (f.Bool)
                {
                    var c = new CheckBox { IsChecked = current == "true", VerticalAlignment = VerticalAlignment.Center };
                    setters.Add(() => file.Set(f.Key, c.IsChecked == true ? "true" : "false", false));
                    editor = c;
                }
                else if (f.Choices != null)
                {
                    var c = new ComboBox { ItemsSource = f.Choices.Contains(current) ? f.Choices : [.. f.Choices, current], SelectedItem = current, Width = 160, HorizontalAlignment = HorizontalAlignment.Left };
                    var numeric = f.Choices.All(x => int.TryParse(x, out _));
                    setters.Add(() => file.Set(f.Key, (string)c.SelectedItem, !numeric));
                    editor = c;
                }
                else
                {
                    var t = new TextBox { Text = current, Width = f.Max > 0 ? 100 : 240, HorizontalAlignment = HorizontalAlignment.Left, Padding = new Thickness(2) };
                    var numeric = f.Max > 0;
                    setters.Add(() =>
                    {
                        if (!numeric) { file.Set(f.Key, t.Text.Trim(), true); return; }
                        if (!int.TryParse(t.Text.Trim(), out var v)) throw new FormatException($"{f.Label}: a whole number");
                        file.Set(f.Key, Math.Clamp(v, f.Min, f.Max).ToString(), false);
                    });
                    editor = t;
                }
                editor.Margin = new Thickness(0, 3, 0, 3);
                Grid.SetRow(editor, row);
                Grid.SetColumn(editor, 1);
                grid.Children.Add(editor);
            }
            section.Children.Add(grid);
        }
        var saved = Line("");
        var footer = new StackPanel();
        footer.Children.Add(Row(Btn("Apply changes", () =>
        {
            if (GameRunning()) { saved.Text = "Close the game first: it would write its own settings over these."; return; }
            try
            {
                foreach (var s in setters) s();
                file.Save();
                saved.Text = $"Saved to {user.SettingsPath}.";
            }
            catch (Exception e) { saved.Text = $"Not saved: {e.Message}"; }
        }), Btn("Open the file", () =>
        {
            if (File.Exists(user.SettingsPath)) Process.Start(new ProcessStartInfo("notepad.exe", $"\"{user.SettingsPath}\""));
            else saved.Text = "There's no settings file yet; the game writes one on its first start.";
        })));
        footer.Children.Add(saved);
        DockPanel.SetDock(footer, Dock.Bottom); shell.Children.Add(footer);
        shell.Children.Add(new ScrollViewer { Content = p, VerticalScrollBarVisibility = ScrollBarVisibility.Auto });
        return shell;
    }

    // docs/MVPMOD.md 5: the newest save's mods that change game data against the ones turned on now; true to go ahead
    bool ModsMatchSaves()
    {
        var newest = user.Saves().FirstOrDefault();
        if (newest == null || user.ModsOf(newest.Slot) is not { } had) return true;
        var on = new ModList(installDir).Mods.Where(m => m.Enabled && m.Error == null).ToList();
        var was = had.Where(m => m.AffectsSaves).Select(m => $"{m.Id} {m.Version}").ToHashSet();
        var now = on.Where(m => m.AffectsSaves).Select(m => $"{m.Id} {m.Version}").ToHashSet();
        if (was.SetEquals(now)) return true;
        string List(IEnumerable<string> l) => l.Any() ? string.Join(", ", l) : "none";
        return MessageBox.Show(this,
            $"Your latest save, {newest.Name}, was made with different mods that change game data (rosters, database).\n\n" +
            $"Then: {List(was)}\nNow: {List(now)}\n\nIt may not load the same way. Play anyway?",
            Title, MessageBoxButton.YesNo, MessageBoxImage.Warning) == MessageBoxResult.Yes;
    }

    // ---- Saves ----

    UIElement SavesTab()
    {
        var p = new StackPanel { Margin = new Thickness(16) };
        p.Children.Add(Line("Your saves", 16, true));
        var saves = user.Saves();
        if (saves.Count == 0) p.Children.Add(Line("No saves yet."));
        else
        {
            var list = new ListView { MaxHeight = 160, Margin = new Thickness(0, 0, 0, 12) };
            foreach (var s in saves)
            {
                var mods = user.ModsOf(s.Slot);
                var with = mods is { Count: > 0 } ? "    mods: " + string.Join(", ", mods.Select(m => $"{m.Id} {m.Version}")) : "";
                list.Items.Add($"{s.Name}    {s.Changed:g}" + (s.History > 0 ? $"    ({s.History} earlier versions kept)" : "") + with);
            }
            p.Children.Add(list);
        }

        p.Children.Add(Line("Restore", 16, true));
        p.Children.Add(Line("The game copies all saves at every start and exit (backups), and keeps each save as it was after its last complete save (last good). Restoring backs up the saves as they are now first, so nothing is lost; then the game starts.", 13));
        var backups = user.Backups();
        var blist = new ListView { MaxHeight = 200, Margin = new Thickness(0, 0, 0, 8) };
        foreach (var b in backups) blist.Items.Add(new ListViewItem { Content = $"{b.Time:g}    {b.Why}    {b.Bytes / 1024} KB", Tag = b.Folder });
        if (backups.Count == 0) p.Children.Add(Line("No backups yet."));
        else p.Children.Add(blist);
        var lastGood = Btn("Restore last good saves", () => Restore("lastgood", "every save as it was after its last complete save"));
        lastGood.IsEnabled = user.HasLastGood;
        var restore = Btn("Restore selected backup", () =>
        {
            if (blist.SelectedItem is ListViewItem { Tag: string folder }) Restore(folder, $"the backup from {((ListViewItem)blist.SelectedItem).Content}");
            else MessageBox.Show(this, "Pick a backup in the list first.", Title);
        });
        restore.IsEnabled = backups.Count > 0;
        p.Children.Add(Row(lastGood, restore, Btn("Open saves folder", () => Open(user.Dir))));
        return new ScrollViewer { Content = p, VerticalScrollBarVisibility = ScrollBarVisibility.Auto };
    }

    // the exe does the restore (--restore-backup, save_safety.cpp), tells the player what happened, then plays
    void Restore(string name, string what)
    {
        if (MessageBox.Show(this, $"Replace your saves with {what}? The current saves are backed up first. The game starts afterwards.", Title,
                MessageBoxButton.YesNo, MessageBoxImage.Question) != MessageBoxResult.Yes) return;
        Start($"--restore-backup \"{name}\"");
    }
}



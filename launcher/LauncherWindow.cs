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
        var saves = new TabItem { Header = "SAVES" };
        tabs.Items.Add(saves);
        tabs.SelectionChanged += (_, e) => { if (e.Source == tabs && saves.IsSelected) saves.Content = new ScrollViewer { Content = SavesTab() }; };
    }

    // ---- Mods (docs/MVPMOD.md: the launcher writes mods\mods.json, the game reads it at its next start) ----

    UIElement ModsTab()
    {
        var mods = new ModList(installDir);
        var list = new ListBox();
        var order = new ListBox { VerticalAlignment = VerticalAlignment.Top };
        var search = new TextBox { Padding = new Thickness(32, 5, 8, 5), VerticalContentAlignment = VerticalAlignment.Center };
        var filter = new ComboBox { Width = 142, Margin = new Thickness(12, 0, 0, 0) };
        foreach (var label in new[] { "All Mods", "Enabled", "Disabled", "Unavailable" }) filter.Items.Add(label);
        filter.SelectedIndex = 0;
        var problems = new TextBlock { TextWrapping = TextWrapping.Wrap, Foreground = Ink("#FF7C8D") };
        string? selectedPath = null;
        bool filling = false;
        ModList.Mod? Selected() => mods.Mods.FirstOrDefault(m => m.Path == selectedPath);

        void Changed()
        {
            try { mods.Save(); }
            catch (Exception e) when (e is IOException or UnauthorizedAccessException) { MessageBox.Show(this, "Couldn't save the mod list: " + e.Message, Title); }
            Fill();
        }

        void ShowDetails(ModList.Mod m)
        {

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
            MessageBox.Show(this, t, "Mod details");
        }

        TextBlock Text(string value, int size = 19, string color = "#EDF0F3") => new()
        {
            Text = value, FontFamily = displayFont, FontSize = size, Foreground = Ink(color),
            VerticalAlignment = VerticalAlignment.Center, TextTrimming = TextTrimming.CharacterEllipsis
        };

        Button ActionButton(string label, Action? action = null)
        {
            var button = Btn(label, action ?? (() => { }));
            button.FontFamily = displayFont;
            button.FontSize = 16;
            button.MinWidth = 0;
            button.Height = 39;
            button.Margin = new Thickness(0);
            button.Padding = new Thickness(11, 0, 11, 0);
            return button;
        }

        var remove = ActionButton("⊗   REMOVE SELECTED");
        var up = ActionButton("⌃");
        var down = ActionButton("⌄");
        up.ToolTip = "Raise priority";
        down.ToolTip = "Lower priority";
        var disable = ActionButton("⊘   DISABLE ALL");
        remove.Click += (_, _) => RemoveMod();
        up.Click += (_, _) => Move(1);
        down.Click += (_, _) => Move(-1);
        disable.Click += (_, _) =>
        {
            foreach (var m in mods.Mods.Where(m => !m.Required)) m.Enabled = false;
            Changed();
        };

        CheckBox Toggle(ModList.Mod mod)
        {
            var box = new CheckBox { IsChecked = mod.Enabled, IsEnabled = mod.Error == null && !mod.Required,
                VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(0, 0, 14, 0) };
            System.Windows.Automation.AutomationProperties.SetName(box, "Enable " + mod.Name);
            box.Click += (_, _) =>
            {
                selectedPath = mod.Path;
                mod.Enabled = box.IsChecked == true;
                Changed();
            };
            return box;
        }

        void SelectRows()
        {
            foreach (var box in new[] { list, order })
                box.SelectedItem = box.Items.OfType<ListBoxItem>().FirstOrDefault(i => i.Tag is ModList.Mod m && m.Path == selectedPath);
            var mod = Selected();
            var rank = order.SelectedIndex;
            up.IsEnabled = rank > 0;
            down.IsEnabled = rank >= 0 && rank < order.Items.Count - 1;
            remove.IsEnabled = mod is { Required: false } && (list.SelectedItem != null || order.SelectedItem != null);
            disable.IsEnabled = mods.Mods.Any(m => m.Enabled && !m.Required);
        }

        void Reorder(ModList.Mod source, ModList.Mod target)
        {
            if (source == target) return;
            var index = mods.Mods.IndexOf(target);
            mods.Mods.Remove(source);
            mods.Mods.Insert(index, source);
            selectedPath = source.Path;
            Changed();
        }

        void Fill()
        {
            filling = true;
            list.Items.Clear();
            order.Items.Clear();
            // The runtime applies overrides last-wins. Display highest priority first.
            var rank = 0;
            foreach (var mod in mods.Mods.AsEnumerable().Reverse().Where(m => m.Enabled && m.Error == null))
            {
                var row = new DockPanel();
                var number = Text((++rank).ToString(), 17);
                number.HorizontalAlignment = HorizontalAlignment.Center;
                row.Children.Add(new Border { Width = 36, Height = 35, Background = Ink("#222D36"),
                    CornerRadius = new CornerRadius(2), Margin = new Thickness(0, 0, 16, 0), Child = number });
                row.Children.Add(Toggle(mod));
                var grip = Text("⠿", 22, "#8998A5");
                grip.Cursor = System.Windows.Input.Cursors.SizeAll;
                grip.ToolTip = "Drag to change priority";
                DockPanel.SetDock(grip, Dock.Right);
                row.Children.Add(grip);
                grip.MouseMove += (_, e) =>
                {
                    if (e.LeftButton == System.Windows.Input.MouseButtonState.Pressed)
                        DragDrop.DoDragDrop(grip, new DataObject("MvpModPath", mod.Path), DragDropEffects.Move);
                };
                row.Children.Add(Text(mod.Name));
                var item = new ListBoxItem { Content = row, Tag = mod, Padding = new Thickness(3, 2, 12, 2), AllowDrop = true };
                item.Drop += (_, e) =>
                {
                    if (e.Data.GetData("MvpModPath") is not string path) return;
                    if (mods.Mods.FirstOrDefault(m => m.Path == path) is { } source) Reorder(source, mod);
                    e.Handled = true;
                };
                order.Items.Add(item);
            }

            var query = search.Text.Trim();
            foreach (var mod in mods.Mods)
            {
                if (!mod.Name.Contains(query, StringComparison.OrdinalIgnoreCase) &&
                    !mod.Description.Contains(query, StringComparison.OrdinalIgnoreCase)) continue;
                if (filter.SelectedIndex == 1 && !mod.Enabled ||
                    filter.SelectedIndex == 2 && (mod.Enabled || mod.Error != null) ||
                    filter.SelectedIndex == 3 && mod.Error == null) continue;

                var row = new DockPanel();
                row.Children.Add(Toggle(mod));
                var more = ActionButton("•••", () => ShowDetails(mod));
                more.Background = Brushes.Transparent;
                more.BorderThickness = new Thickness(0);
                more.ToolTip = "Mod details";
                System.Windows.Automation.AutomationProperties.SetName(more, "Details for " + mod.Name);
                DockPanel.SetDock(more, Dock.Right);
                row.Children.Add(more);
                var name = new StackPanel { VerticalAlignment = VerticalAlignment.Center };
                name.Children.Add(Text(mod.Name));
                var metadata = string.Join("  |  ", new[]
                {
                    mod.Version == "" ? "" : $"v{mod.Version}", mod.Author,
                    mod.Error != null ? "Unavailable" : mod.Required ? "Required" : ""
                }.Where(value => value != ""));
                if (metadata != "") name.Children.Add(Text(metadata, 15, mod.Error == null ? "#AAB7C2" : "#FF7C8D"));
                row.Children.Add(name);
                list.Items.Add(new ListBoxItem { Content = row, Tag = mod, MinHeight = 47 });
            }
            if (list.Items.Count == 0) list.Items.Add(new ListBoxItem { IsEnabled = false,
                Content = mods.Mods.Count == 0 ? "No mods installed. Use Install Mod to add one." : "No matching mods." });
            if (order.Items.Count == 0) order.Items.Add(new ListBoxItem { IsEnabled = false, Content = "No enabled mods." });
            problems.Text = string.Join("\n", mods.Problems().Distinct());
            problems.Visibility = problems.Text.Length == 0 ? Visibility.Collapsed : Visibility.Visible;
            SelectRows();
            filling = false;
        }

        void Select(ListBox box)
        {
            if (filling || box.SelectedItem is not ListBoxItem { Tag: ModList.Mod mod }) return;
            filling = true;
            selectedPath = mod.Path;
            SelectRows();
            filling = false;
        }

        list.SelectionChanged += (_, _) => Select(list);
        order.SelectionChanged += (_, _) => Select(order);
        search.TextChanged += (_, _) => Fill();
        filter.SelectionChanged += (_, _) => Fill();

        void Move(int by)
        {
            var rank = order.SelectedIndex;
            var targetRank = rank - by;
            if (rank >= 0 && targetRank >= 0 && targetRank < order.Items.Count &&
                order.Items[rank] is ListBoxItem { Tag: ModList.Mod source } &&
                order.Items[targetRank] is ListBoxItem { Tag: ModList.Mod target }) Reorder(source, target);
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
            if (Selected() is not { } m) return;
            if (m.Required) { MessageBox.Show(this, "This league mod is required. Use Repair to restore it; it cannot be removed or disabled here.", Title); return; }
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

        var p = new Grid { Margin = new Thickness(40, 14, 40, 30), Background = Ink("#0A1015") };
        p.Resources.MergedDictionaries.Add(ModStyles());
        p.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1.37, GridUnitType.Star) });
        p.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(48) });
        p.ColumnDefinitions.Add(new ColumnDefinition());
        var divider = new Border { Width = 1, Background = Ink("#35414D"), HorizontalAlignment = HorizontalAlignment.Center };
        Grid.SetColumn(divider, 1);
        p.Children.Add(divider);

        void AddDocked(DockPanel panel, UIElement child, Dock edge)
        {
            DockPanel.SetDock(child, edge);
            panel.Children.Add(child);
        }

        StackPanel Header(string title, string description)
        {
            var heading = Heading(title, 29);
            heading.Margin = new Thickness(0);
            var subtitle = Text(description, 17, "#AAB7C2");
            subtitle.TextWrapping = TextWrapping.Wrap;
            return new StackPanel { Children = { heading, subtitle } };
        }

        var left = new DockPanel();
        var right = new DockPanel();
        Grid.SetColumn(right, 2);
        p.Children.Add(left);
        p.Children.Add(right);
        var header = new DockPanel();
        var install = ActionButton("＋   INSTALL MOD", AddMod);
        install.FontWeight = FontWeights.Bold;
        install.Background = new LinearGradientBrush(Color.FromRgb(221, 5, 24), Color.FromRgb(160, 0, 11), 90);
        install.BorderBrush = Ink("#F40A1B");
        install.VerticalAlignment = VerticalAlignment.Top;
        AddDocked(header, install, Dock.Right);
        header.Children.Add(Header("MODS", "Manage installed mods, rosters, textures, and custom content."));
        AddDocked(left, header, Dock.Top);

        var filters = new DockPanel { Height = 38, Margin = new Thickness(0, 13, 0, 9) };
        AddDocked(filters, filter, Dock.Right);
        var field = new Grid();
        field.Children.Add(search);
        var placeholder = Text("Search mods…", 17, "#82909D");
        placeholder.Margin = new Thickness(32, 0, 0, 0);
        placeholder.IsHitTestVisible = false;
        search.TextChanged += (_, _) => placeholder.Visibility = search.Text.Length == 0 ? Visibility.Visible : Visibility.Collapsed;
        System.Windows.Automation.AutomationProperties.SetName(search, "Search mods");
        field.Children.Add(placeholder);
        var magnifier = Text("\uE721", 15);
        magnifier.FontFamily = new FontFamily("Segoe MDL2 Assets");
        magnifier.Margin = new Thickness(10, 0, 0, 0);
        magnifier.IsHitTestVisible = false;
        field.Children.Add(magnifier);
        filters.Children.Add(field);
        AddDocked(left, filters, Dock.Top);

        var leftActions = new DockPanel { Margin = new Thickness(0, 12, 0, 0) };
        AddDocked(leftActions, disable, Dock.Right);
        var folder = ActionButton("OPEN MODS FOLDER", () => Open(mods.Dir));
        var refresh = ActionButton("↻   REFRESH MODS", () => { mods.Reload(); Fill(); });
        folder.Margin = refresh.Margin = new Thickness(0, 0, 8, 0);
        leftActions.Children.Add(new WrapPanel { Children = { folder, refresh } });
        AddDocked(left, leftActions, Dock.Bottom);
        left.Children.Add(list);

        var orderHeader = Header("LOAD ORDER", "Drag to change the load order. Top mods have higher priority.");
        orderHeader.Margin = new Thickness(0, 0, 0, 15);
        AddDocked(right, orderHeader, Dock.Top);
        var rightActions = new DockPanel { Margin = new Thickness(0, 12, 0, 0) };
        AddDocked(rightActions, remove, Dock.Right);
        up.Width = down.Width = 40;
        up.Margin = new Thickness(0, 0, 8, 0);
        rightActions.Children.Add(new StackPanel { Orientation = Orientation.Horizontal, Children = { up, down } });
        AddDocked(right, rightActions, Dock.Bottom);
        var warnings = new ScrollViewer { Content = problems, MaxHeight = 90, VerticalScrollBarVisibility = ScrollBarVisibility.Auto };
        warnings.SetBinding(VisibilityProperty, new System.Windows.Data.Binding(nameof(Visibility)) { Source = problems });
        AddDocked(right, warnings, Dock.Bottom);
        right.Children.Add(order);

        var play = new MenuItem { Header = "Play without optional mods" };
        play.Click += (_, _) => Start("--no-mods");
        p.ContextMenu = new ContextMenu { Items = { play } };
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
        ("Dynasty", [
            new("fitted_poll", "Poll fitted to the real Top 25", "false", Bool: true, Help: "Always on with an expanded (2027) database"),
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



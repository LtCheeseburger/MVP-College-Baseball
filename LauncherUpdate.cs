using System.Diagnostics;
using System.Net.Http;
using System.Windows;
using System.Windows.Controls;
using MVPWorkshop.Core.Install;

namespace MVPSetup;

// The launcher's updater: a banner when a newer release is on GitHub, installed with one click (PortUpdater)
sealed partial class LauncherWindow
{
    readonly Border updateBanner = new() { Visibility = Visibility.Collapsed, Padding = new Thickness(30, 12, 30, 12), BorderThickness = new Thickness(0, 1, 0, 0) };
    readonly TextBlock updateText = new() { TextWrapping = TextWrapping.Wrap, VerticalAlignment = VerticalAlignment.Center, FontSize = 15 };
    readonly StackPanel updateButtons = new() { Orientation = Orientation.Horizontal, VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(16, 0, 0, 0) };
    readonly ProgressBar updateBar = new() { Height = 6, Maximum = 1, Margin = new Thickness(0, 8, 0, 0), Visibility = Visibility.Collapsed };
    PortRelease? pending;
    bool updating;

    string InstalledVersion => PortUpdater.InstalledVersion(installDir);

    /// <summary>Along the bottom of the window (the tabs sit up in the header): the banner, then a check once the window is up.</summary>
    void SetUpUpdates()
    {
        updateBanner.Background = Ink("#2A0D14");
        updateBanner.BorderBrush = Ink("#ED0026");
        var body = new DockPanel();
        DockPanel.SetDock(updateButtons, Dock.Right);
        body.Children.Add(updateButtons);
        var text = new StackPanel();
        text.Children.Add(updateText);
        text.Children.Add(updateBar);
        body.Children.Add(text);
        updateBanner.Child = body;
        if (Content is DockPanel root)
        {
            DockPanel.SetDock(updateBanner, Dock.Bottom);
            root.Children.Insert(1, updateBanner);
        }

        // an update cut off half way is put back; a finished one leaves its backup to remove
        if (PortUpdater.Tidy(installDir)) ShowBanner("The last update didn't finish, so the previous version was put back.", ("OK", HideBanner));
        var args = Environment.GetCommandLineArgs();
        if (Array.IndexOf(args, "--updated") is var i and >= 0)
            ShowBanner($"Updated to version {InstalledVersion}" + (i + 1 < args.Length && !args[i + 1].StartsWith("--") ? $" (was {args[i + 1]})." : "."), ("OK", HideBanner));
        Loaded += async (_, _) => await CheckForUpdates(quiet: true);
    }

    void ShowBanner(string text, params (string Label, Action Click)[] buttons)
    {
        updateText.Text = text;
        updateButtons.Children.Clear();
        foreach (var (label, click) in buttons)
        {
            var b = Btn(label, click);
            b.Margin = new Thickness(8, 0, 0, 0);
            updateButtons.Children.Add(b);
        }
        updateBanner.Visibility = Visibility.Visible;
    }

    void HideBanner() => updateBanner.Visibility = Visibility.Collapsed;

    /// <summary>Asks GitHub for the newest release (pre-releases included); quiet: say nothing when up to date or offline.</summary>
    async Task CheckForUpdates(bool quiet)
    {
        if (updating) return;
        try
        {
            pending = await PortUpdater.CheckAsync(InstalledVersion);
            if (pending is null)
            {
                if (!quiet) ShowBanner($"You have the latest version ({InstalledVersion}).", ("OK", HideBanner));
                return;
            }
            ShowBanner($"Version {pending.Version} is available. You have {InstalledVersion}.",
                ("What's new", WhatsNew), ("Update now", () => _ = UpdateNow()), ("Later", HideBanner));
        }
        catch (Exception e) when (e is HttpRequestException or TaskCanceledException or System.Text.Json.JsonException or InvalidOperationException)
        {
            if (!quiet) ShowBanner($"Couldn't check for updates ({e.Message}).", ("OK", HideBanner));
        }
    }

    void WhatsNew()
    {
        if (pending is null) return;
        var notes = pending.Notes.Trim() == "" ? "No notes for this release." : pending.Notes.Trim();
        if (MessageBox.Show(this, $"{pending.Name}\n\n{notes}\n\nOpen the release page?", Title, MessageBoxButton.YesNo, MessageBoxImage.Information) == MessageBoxResult.Yes)
            Process.Start(new ProcessStartInfo(pending.PageUrl) { UseShellExecute = true });
    }

    async Task UpdateNow()
    {
        if (pending is not { } release || updating) return;
        if (GameRunning()) { MessageBox.Show(this, "Close the game first, then update.", Title); return; }
        if (PortUpdater.InstallProblem(installDir) is { } why) { MessageBox.Show(this, why, Title); return; }
        var from = InstalledVersion;
        updating = true;
        updateButtons.Children.Clear();
        updateBar.Visibility = Visibility.Visible;
        updateBar.Value = 0;
        try
        {
            updateText.Text = $"Downloading version {release.Version}…";
            var progress = new Progress<(long Done, long? Total)>(p =>
            {
                updateBar.Value = p.Total is > 0 ? (double)p.Done / p.Total.Value : 0;
                updateText.Text = $"Downloading version {release.Version}… {p.Done >> 20} MB" + (p.Total is > 0 ? $" of {p.Total.Value >> 20} MB" : "");
            });
            var zip = await PortUpdater.DownloadAsync(release, progress);
            updateBar.IsIndeterminate = true;
            var steps = new Progress<string>(s => updateText.Text = $"{s}…");
            await Task.Run(() => PortUpdater.Apply(installDir, zip, steps));
            // the new launcher removes the backup (with this exe) and says what happened
            Process.Start(new ProcessStartInfo(Path.Combine(installDir, Game.Launcher), $"--updated {from}") { WorkingDirectory = installDir });
            Close();
        }
        catch (Exception e)
        {
            updateBar.IsIndeterminate = false;
            updateBar.Visibility = Visibility.Collapsed;
            ShowBanner($"The update didn't install, and nothing was changed: {e.Message}", ("Try again", () => _ = UpdateNow()), ("Later", HideBanner));
        }
        finally { updating = false; }
    }
}

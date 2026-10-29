using System.IO.Compression;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Markup;
using System.Windows.Media;
using System.Windows.Media.Imaging;

namespace MVPSetup;

sealed partial class LauncherWindow
{
    static SolidColorBrush Ink(string hex) => new((Color)ColorConverter.ConvertFromString(hex));
    readonly FontFamily displayFont = new(new Uri("pack://application:,,,/"), "./Setup;component/Assets/#Barlow Condensed");

    void BuildShell()
    {
        Width = 1280; Height = 850; MinWidth = 1050; MinHeight = 700;
        Background = Ink("#080E12"); Foreground = Ink("#EDF0F3"); FontFamily = new FontFamily("Segoe UI"); FontSize = 14;
        Resources.MergedDictionaries.Add((ResourceDictionary)XamlReader.Parse("""
        <ResourceDictionary xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml">
          <Style TargetType="Button">
            <Setter Property="Foreground" Value="#EEF1F4"/><Setter Property="Background" Value="#131D24"/><Setter Property="BorderBrush" Value="#40515D"/><Setter Property="BorderThickness" Value="1"/><Setter Property="Cursor" Value="Hand"/>
            <Setter Property="Template"><Setter.Value><ControlTemplate TargetType="Button">
              <Border x:Name="frame" Background="{TemplateBinding Background}" BorderBrush="{TemplateBinding BorderBrush}" BorderThickness="{TemplateBinding BorderThickness}" Padding="{TemplateBinding Padding}" CornerRadius="3"><ContentPresenter HorizontalAlignment="{TemplateBinding HorizontalContentAlignment}" VerticalAlignment="Center"/></Border>
              <ControlTemplate.Triggers><Trigger Property="IsMouseOver" Value="True"><Setter TargetName="frame" Property="BorderBrush" Value="#EF233C"/></Trigger><Trigger Property="IsKeyboardFocused" Value="True"><Setter TargetName="frame" Property="BorderBrush" Value="White"/></Trigger><Trigger Property="IsEnabled" Value="False"><Setter Property="Opacity" Value="0.4"/></Trigger></ControlTemplate.Triggers>
            </ControlTemplate></Setter.Value></Setter>
          </Style>
          <Style TargetType="TextBox"><Setter Property="Background" Value="#131D24"/><Setter Property="Foreground" Value="#EEF1F4"/><Setter Property="CaretBrush" Value="White"/><Setter Property="BorderBrush" Value="#40515D"/><Setter Property="Padding" Value="8,6"/></Style>
          <Style TargetType="ComboBoxItem"><Setter Property="Foreground" Value="#EDF0F3"/><Setter Property="Background" Value="#18242D"/><Setter Property="Padding" Value="10,7"/></Style>
          <Style TargetType="ComboBox"><Setter Property="Foreground" Value="#EDF0F3"/><Setter Property="Background" Value="#131D24"/><Setter Property="MinHeight" Value="32"/>
            <Setter Property="Template"><Setter.Value><ControlTemplate TargetType="ComboBox"><Grid>
              <ToggleButton Focusable="False" IsChecked="{Binding IsDropDownOpen, RelativeSource={RelativeSource TemplatedParent}, Mode=TwoWay}"><ToggleButton.Template><ControlTemplate TargetType="ToggleButton"><Border Background="#131D24" BorderBrush="#40515D" BorderThickness="1" CornerRadius="2"><TextBlock Text="⌄" Foreground="#EDF0F3" HorizontalAlignment="Right" Margin="0,0,10,0" VerticalAlignment="Center"/></Border></ControlTemplate></ToggleButton.Template></ToggleButton>
              <ContentPresenter Content="{TemplateBinding SelectionBoxItem}" Margin="10,5,30,5" IsHitTestVisible="False" VerticalAlignment="Center"/>
              <Popup x:Name="PART_Popup" IsOpen="{TemplateBinding IsDropDownOpen}" Placement="Bottom" AllowsTransparency="True" Focusable="False"><Border Background="#18242D" BorderBrush="#40515D" BorderThickness="1" MinWidth="{Binding ActualWidth, RelativeSource={RelativeSource TemplatedParent}}"><ScrollViewer MaxHeight="320"><ItemsPresenter/></ScrollViewer></Border></Popup>
            </Grid></ControlTemplate></Setter.Value></Setter>
          </Style>
          <Style TargetType="CheckBox"><Setter Property="Foreground" Value="#D9E0E6"/><Setter Property="VerticalContentAlignment" Value="Center"/><Setter Property="Template"><Setter.Value><ControlTemplate TargetType="CheckBox"><StackPanel Orientation="Horizontal"><Border x:Name="box" Width="20" Height="20" Background="#101A21" BorderBrush="#627480" BorderThickness="1" CornerRadius="2"><TextBlock x:Name="mark" Text="✓" Foreground="White" FontWeight="Bold" HorizontalAlignment="Center" VerticalAlignment="Center" Visibility="Collapsed"/></Border><ContentPresenter Margin="8,0,0,0" VerticalAlignment="Center"/></StackPanel><ControlTemplate.Triggers><Trigger Property="IsChecked" Value="True"><Setter TargetName="box" Property="Background" Value="#E60024"/><Setter TargetName="mark" Property="Visibility" Value="Visible"/></Trigger><Trigger Property="IsKeyboardFocused" Value="True"><Setter TargetName="box" Property="BorderBrush" Value="White"/></Trigger><Trigger Property="IsEnabled" Value="False"><Setter Property="Opacity" Value="0.4"/></Trigger></ControlTemplate.Triggers></ControlTemplate></Setter.Value></Setter></Style>
          <Style TargetType="ListBox"><Setter Property="Background" Value="#0B1319"/><Setter Property="Foreground" Value="#EDF0F3"/><Setter Property="BorderBrush" Value="#34434E"/><Setter Property="ScrollViewer.HorizontalScrollBarVisibility" Value="Disabled"/></Style>
          <Style TargetType="ListBoxItem"><Setter Property="HorizontalContentAlignment" Value="Stretch"/><Setter Property="Padding" Value="12,10"/><Setter Property="BorderThickness" Value="0,0,0,1"/><Setter Property="BorderBrush" Value="#24333D"/>
            <Setter Property="Template"><Setter.Value><ControlTemplate TargetType="ListBoxItem"><Border x:Name="frame" Padding="{TemplateBinding Padding}" Background="{TemplateBinding Background}" BorderBrush="{TemplateBinding BorderBrush}" BorderThickness="{TemplateBinding BorderThickness}"><ContentPresenter/></Border><ControlTemplate.Triggers><Trigger Property="IsSelected" Value="True"><Setter TargetName="frame" Property="Background" Value="#52101D"/><Setter TargetName="frame" Property="BorderBrush" Value="#ED0026"/></Trigger><Trigger Property="IsMouseOver" Value="True"><Setter TargetName="frame" Property="Background" Value="#25313B"/></Trigger></ControlTemplate.Triggers></ControlTemplate></Setter.Value></Setter>
          </Style>
          <Style TargetType="TabControl"><Setter Property="Background" Value="Transparent"/><Setter Property="BorderThickness" Value="0"/><Setter Property="Template"><Setter.Value><ControlTemplate TargetType="TabControl"><DockPanel><TabPanel IsItemsHost="True" DockPanel.Dock="Top" HorizontalAlignment="Right" Margin="0,-58,28,20"/><ContentPresenter ContentSource="SelectedContent"/></DockPanel></ControlTemplate></Setter.Value></Setter></Style>
          <Style TargetType="TabItem"><Setter Property="Foreground" Value="#B6C1CB"/><Setter Property="FontSize" Value="19"/><Setter Property="Template"><Setter.Value><ControlTemplate TargetType="TabItem"><Border x:Name="tab" BorderThickness="0,0,0,4" BorderBrush="Transparent" Padding="18,12"><ContentPresenter ContentSource="Header"/></Border><ControlTemplate.Triggers><Trigger Property="IsSelected" Value="True"><Setter TargetName="tab" Property="BorderBrush" Value="#EE0027"/><Setter Property="Foreground" Value="White"/></Trigger><Trigger Property="IsMouseOver" Value="True"><Setter Property="Foreground" Value="White"/></Trigger></ControlTemplate.Triggers></ControlTemplate></Setter.Value></Setter></Style>
        </ResourceDictionary>
        """));
        var root = new DockPanel();
        var header = new Grid { Height = 148, Background = new LinearGradientBrush(Color.FromRgb(23,32,40), Color.FromRgb(7,12,16), 15) };
        var brand = new StackPanel { Orientation = Orientation.Horizontal, Margin = new Thickness(30,8,0,30), VerticalAlignment = VerticalAlignment.Center };
        brand.Children.Add(new TextBlock { Text = "MVP", FontFamily = displayFont, FontSize = 94, FontWeight = FontWeights.Black, FontStyle = FontStyles.Italic });
        brand.Children.Add(new TextBlock { Text = "27", FontFamily = displayFont, FontSize = 94, FontWeight = FontWeights.Black, FontStyle = FontStyles.Italic, Foreground = Ink("#ED0026") });
        brand.Children.Add(new Border { Width = 3, Background = Ink("#ED0026"), Margin = new Thickness(15,15,15,10) });
        brand.Children.Add(new TextBlock { Text = "COLLEGE\nBASEBALL", FontFamily = displayFont, FontSize = 30, FontWeight = FontWeights.Bold, FontStyle = FontStyles.Italic, VerticalAlignment = VerticalAlignment.Center, LineHeight = 30 });
        header.Children.Add(brand);
        var top = new Border { Child = header, BorderBrush = Ink("#34434E"), BorderThickness = new Thickness(0,0,0,1) };
        DockPanel.SetDock(top, Dock.Top); root.Children.Add(top);
        tabs.Margin = new Thickness(0); root.Children.Add(tabs); Content = root;
    }

    TextBlock Heading(string text, double size = 28) => new() { Text = text, FontFamily = displayFont, FontSize = size, FontWeight = FontWeights.Bold, Margin = new Thickness(0,0,0,8) };
    static Border Panel(UIElement content) => new() { Child = content, Background = Ink("#0C141A"), BorderBrush = Ink("#34434E"), BorderThickness = new Thickness(1), CornerRadius = new CornerRadius(3), Padding = new Thickness(22), Margin = new Thickness(0,0,0,16) };

    UIElement HomeTab()
    {
        var grid = new Grid { Margin = new Thickness(30) };
        grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1.15, GridUnitType.Star) });
        grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(28) });
        grid.ColumnDefinitions.Add(new ColumnDefinition());
        var actions = new StackPanel();
        void Card(string icon, string title, string description, Action action, bool primary = false)
        {
            var content = new DockPanel();
            var glyph = new TextBlock { Text = icon, FontFamily = new FontFamily("Segoe MDL2 Assets"), FontSize = 34, Width = 70, VerticalAlignment = VerticalAlignment.Center };
            content.Children.Add(glyph);
            var text = new StackPanel(); text.Children.Add(Heading(title, 34)); text.Children.Add(Line(description, 13)); content.Children.Add(text);
            var button = Btn("", action); button.Content = content; button.HorizontalContentAlignment = HorizontalAlignment.Stretch; button.Padding = new Thickness(24,22,24,22); button.Margin = new Thickness(0,0,0,18);
            if (primary) { button.Background = new LinearGradientBrush(Color.FromRgb(197,0,29), Color.FromRgb(106,0,19), 0); button.BorderBrush = Ink("#FF173B"); }
            actions.Children.Add(button);
        }
        var ready = Check(out var problem, out var note);
        Card("\uE768", "PLAY", "Start your next season, tournament, or exhibition.", () => { if (Check(out var why, out _) && ModsMatchSaves()) Start(""); else if (why != "") MessageBox.Show(this, why, Title); }, true);
        Card("\uE8B7", "MODS", "Manage rosters, uniforms, stadiums, and more.", () => tabs.SelectedIndex = 1);
        Card("\uE713", "SETTINGS", "Configure display, graphics, sound, and controls.", () => tabs.SelectedIndex = 2);
        var state = Line(ready ? note : problem, 13); state.Foreground = Ink(ready ? "#AABBC9" : "#FF7C8D"); actions.Children.Add(state); grid.Children.Add(actions);
        var tools = new StackPanel(); tools.Children.Add(Heading("YOUR COLLEGE BASEBALL HUB")); tools.Children.Add(Line("Build your dynasty. Make the game your own.", 15)); tools.Children.Add(PlayTab());
        var right = Panel(tools); Grid.SetColumn(right, 2); grid.Children.Add(right);
        return new ScrollViewer { Content = grid, VerticalScrollBarVisibility = ScrollBarVisibility.Auto };
    }

    static UIElement ModIcon(ModList.Mod mod)
    {
        // Optional artwork lives at the package root, outside the game-file overrides.
        try
        {
            using var data = new MemoryStream();
            if (Directory.Exists(mod.Path))
            {
                var path = Path.Combine(mod.Path, "icon.png");
                if (!File.Exists(path)) path = Path.Combine(mod.Path, "icon.jpg");
                if (!File.Exists(path) || new FileInfo(path).Length > 4 * 1024 * 1024) return Fallback();
                using var input = File.OpenRead(path); input.CopyTo(data);
            }
            else
            {
                using var zip = ZipFile.OpenRead(mod.Path);
                var entry = zip.GetEntry("icon.png") ?? zip.GetEntry("icon.jpg");
                if (entry == null || entry.Length > 4 * 1024 * 1024) return Fallback();
                using var input = entry.Open(); input.CopyTo(data);
            }
            data.Position = 0;
            var image = new BitmapImage(); image.BeginInit(); image.CacheOption = BitmapCacheOption.OnLoad; image.DecodePixelWidth = 96; image.StreamSource = data; image.EndInit(); image.Freeze();
            return Frame(new Image { Source = image, Stretch = Stretch.UniformToFill });
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException or NotSupportedException or ArgumentException or System.IO.FileFormatException) { return Fallback(); }
        Border Frame(UIElement child) => new() { Width = 48, Height = 48, Child = child, Background = Ink("#25333F"), Margin = new Thickness(0,0,14,0), ClipToBounds = true };
        UIElement Fallback() => Frame(new TextBlock { Text = mod.Name.Length > 0 ? mod.Name[..1].ToUpperInvariant() : "M", Foreground = Ink("#CCD6DF"), FontSize = 25, FontWeight = FontWeights.Bold, HorizontalAlignment = HorizontalAlignment.Center, VerticalAlignment = VerticalAlignment.Center });
    }
}

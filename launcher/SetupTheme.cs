using System.Windows;
using System.Windows.Controls;
using System.Windows.Markup;
using System.Windows.Media;

namespace MVPSetup;

sealed partial class SetupWindow
{
    static SolidColorBrush Ink(string color) => new((Color)ColorConverter.ConvertFromString(color));
    readonly StackPanel steps = new();
    readonly Grid body = new();
    readonly Grid footer = new();
    readonly FontFamily displayFont = new(new Uri("pack://application:,,,/"), "./Setup;component/Assets/#Barlow Condensed");

    void BuildShell()
    {
        Width = 1152; Height = 768; MinWidth = 900; MinHeight = 650;
        Background = Ink("#080E12"); Foreground = Ink("#EEF0F2");
        FontFamily = new FontFamily("Segoe UI"); FontSize = 18;
        Resources.Add(typeof(Button), (Style)XamlReader.Parse("""
            <Style xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" TargetType="Button">
              <Setter Property="Foreground" Value="#F4F5F6"/><Setter Property="Background" Value="#141D24"/>
              <Setter Property="BorderBrush" Value="#55616B"/><Setter Property="BorderThickness" Value="1"/>
              <Setter Property="Padding" Value="20,12"/><Setter Property="Cursor" Value="Hand"/>
              <Setter Property="Template"><Setter.Value><ControlTemplate TargetType="Button">
                <Border x:Name="Frame" xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml" Background="{TemplateBinding Background}" BorderBrush="{TemplateBinding BorderBrush}" BorderThickness="{TemplateBinding BorderThickness}" CornerRadius="3" Padding="{TemplateBinding Padding}">
                  <ContentPresenter HorizontalAlignment="Center" VerticalAlignment="Center"/>
                </Border>
                <ControlTemplate.Triggers>
                  <Trigger Property="IsMouseOver" Value="True"><Setter Property="Opacity" Value="0.85"/></Trigger>
                  <Trigger Property="IsKeyboardFocused" Value="True"><Setter Property="BorderBrush" Value="White"/></Trigger>
                  <Trigger Property="IsEnabled" Value="False"><Setter Property="Opacity" Value="0.4"/><Setter Property="Cursor" Value="Arrow"/></Trigger>
                </ControlTemplate.Triggers>
              </ControlTemplate></Setter.Value></Setter>
            </Style>
            """));
        var root = new Grid { Background = Ink("#080E12") };
        root.RowDefinitions.Add(new RowDefinition { Height = new GridLength(138) });
        root.RowDefinitions.Add(new RowDefinition());
        root.RowDefinitions.Add(new RowDefinition { Height = new GridLength(24) });
        var header = new Grid { Background = new LinearGradientBrush(Color.FromRgb(22,30,36), Color.FromRgb(5,10,13), 15) };
        var brand = new StackPanel { Orientation = Orientation.Horizontal, Margin = new Thickness(30, 12, 0, 12), VerticalAlignment = VerticalAlignment.Center };
        brand.Children.Add(new TextBlock { Text = "MVP", FontFamily = displayFont, FontStyle = FontStyles.Italic, FontWeight = FontWeights.Black, FontSize = 88 });
        brand.Children.Add(new TextBlock { Text = "27", Foreground = Ink("#E60024"), FontFamily = displayFont, FontStyle = FontStyles.Italic, FontWeight = FontWeights.Black, FontSize = 88 });
        brand.Children.Add(new Border { Width = 3, Background = Ink("#E60024"), Margin = new Thickness(15,12,15,8) });
        brand.Children.Add(new TextBlock { Text = "COLLEGE\nBASEBALL", FontFamily = displayFont, FontWeight = FontWeights.Bold, FontStyle = FontStyles.Italic, FontSize = 36, LineHeight = 37, VerticalAlignment = VerticalAlignment.Center });
        header.Children.Add(brand);
        header.Children.Add(new TextBlock { Text = "S E T U P   I N S T A L L E R", Foreground = Ink("#7E8993"), FontSize = 14, HorizontalAlignment = HorizontalAlignment.Right, VerticalAlignment = VerticalAlignment.Bottom, Margin = new Thickness(0,0,35,24) });
        root.Children.Add(new Border { Child = header, BorderBrush = Ink("#303D45"), BorderThickness = new Thickness(0,0,0,1) });
        var main = new Grid(); main.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(300) }); main.ColumnDefinitions.Add(new ColumnDefinition());
        Grid.SetRow(main, 1); root.Children.Add(main);
        steps.Margin = new Thickness(20,30,0,0);
        main.Children.Add(new Border { Child = steps, BorderBrush = Ink("#243139"), BorderThickness = new Thickness(0,0,1,0) });
        body.Margin = new Thickness(38,35,38,28);
        body.RowDefinitions.Add(new RowDefinition()); body.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        Grid.SetColumn(body, 1); main.Children.Add(body);
        page.Margin = new Thickness(0);
        body.Children.Add(new ScrollViewer { Content = page, VerticalScrollBarVisibility = ScrollBarVisibility.Auto, HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled });
        footer.Margin = new Thickness(0,20,0,0); Grid.SetRow(footer,1); body.Children.Add(footer);
        Content = root;
        SetStep(0);
    }

    void SetStep(int active)
    {
        steps.Children.Clear();
        string[] names = ["INSTALL LOCATION", "INSTALLING", "COMPLETE"];
        for (int i = 0; i < names.Length; i++)
        {
            var row = new StackPanel { Orientation = Orientation.Horizontal, Margin = new Thickness(18,13,0,13) };
            var color = i == active ? "#E60024" : "#35414A";
            row.Children.Add(new Border { Background = Ink(color), CornerRadius = new CornerRadius(24), Width = 40, Height = 40, Child = new TextBlock { Text = (i+1).ToString(), FontFamily = displayFont, FontWeight = FontWeights.Bold, FontSize = 27, HorizontalAlignment = HorizontalAlignment.Center, VerticalAlignment = VerticalAlignment.Center } });
            row.Children.Add(new TextBlock { Text = names[i], FontFamily = displayFont, FontWeight = FontWeights.Bold, FontSize = 22, Foreground = Ink(i == active ? "#F1F3F4" : "#596670"), VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(14,0,0,0) });
            steps.Children.Add(new Border { BorderBrush = Ink(i == active ? "#E60024" : "#080E12"), BorderThickness = new Thickness(5,0,0,0), Child = row });
        }
    }
}

using DeskInk.PcTool.Models;
using DeskInk.PcTool.Services;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Windows.Storage.Pickers;
using WinRT.Interop;

namespace DeskInk.PcTool.Pages;

public sealed partial class SettingsPage : Page
{
    private readonly AppServices _services = App.Instance.Services;

    public SettingsPage()
    {
        InitializeComponent();
        LoadSettings();
        RefreshPorts();

        _services.DeviceConfig.Changed += RefreshDeviceConfig;
        RefreshDeviceConfig();

        _services.News.Changed += OnNewsChanged;
        UpdateNewsPreview();
    }

    private void OnNewsChanged() => UpdateNewsPreview();

    private void UpdateNewsPreview() => Ui.Post(() =>
    {
        var text = _services.News.Current;
        NewsPreviewText.Text = string.IsNullOrWhiteSpace(text)
            ? $"尚未获取（{_services.News.SourceText}）"
            : $"{_services.News.SourceText}：{text}";
    });

    private NewsSourceKind SelectedNewsSource
        => ((NewsSourceCombo.SelectedItem as ComboBoxItem)?.Tag as string) switch
        {
            "Url" => NewsSourceKind.Url,
            "File" => NewsSourceKind.File,
            "Manual" => NewsSourceKind.Manual,
            _ => NewsSourceKind.Rss
        };

    private void NewsSourceCombo_SelectionChanged(object sender, SelectionChangedEventArgs e)
        => UpdateNewsSourceVisibility();

    private void UpdateNewsSourceVisibility()
    {
        var source = SelectedNewsSource;
        NewsFeedUrlBox.Visibility = source == NewsSourceKind.Rss ? Visibility.Visible : Visibility.Collapsed;
        NewsUrlBox.Visibility = source == NewsSourceKind.Url ? Visibility.Visible : Visibility.Collapsed;
        NewsFileRow.Visibility = source == NewsSourceKind.File ? Visibility.Visible : Visibility.Collapsed;
        NewsManualBox.Visibility = source == NewsSourceKind.Manual ? Visibility.Visible : Visibility.Collapsed;
    }

    private void ApplyNewsFields()
    {
        var settings = _services.Settings.Current;
        settings.PushNews = PushNewsSwitch.IsOn;
        settings.NewsSource = SelectedNewsSource;
        settings.NewsFeedUrl = NewsFeedUrlBox.Text.Trim();
        settings.NewsUrl = NewsUrlBox.Text.Trim();
        settings.NewsFile = NewsFileBox.Text.Trim();
        settings.NewsManual = NewsManualBox.Text;
    }

    private async void BrowseNewsFile_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            var picker = new FileOpenPicker();
            picker.FileTypeFilter.Add("*");

            var window = App.Instance.MainWindow;
            if (window is not null)
            {
                InitializeWithWindow.Initialize(picker, WindowNative.GetWindowHandle(window));
            }

            var file = await picker.PickSingleFileAsync();
            if (file is not null)
            {
                NewsFileBox.Text = file.Path;
            }
        }
        catch (Exception ex)
        {
            _services.Log.Warn($"选择文件失败：{ex.Message}");
        }
    }

    private async void RefreshNews_Click(object sender, RoutedEventArgs e)
    {
        ApplyNewsFields();
        var ok = await _services.News.RefreshAsync("手动刷新");
        UpdateNewsPreview();
        NewsStatusText.Text = ok ? "已刷新，记得保存" : "获取失败，看日志";
    }

    private void PushNews_Click(object sender, RoutedEventArgs e)
    {
        if (!_services.Serial.IsOpen)
        {
            NewsStatusText.Text = "串口未打开";
            return;
        }

        NewsStatusText.Text = _services.Push.PushNews("手动推送") ? "已推送" : "没有可推送的新闻";
    }

    private void RefreshDeviceConfig()
    {
        Ui.Post(() =>
        {
            var config = _services.DeviceConfig.Current;
            if (config is null)
            {
                // 未连接 / 还没收到 cfg：一律显示「—」，绝不显示默认值。
                CfgRefreshText.Text = "—";
                CfgPomoFocusText.Text = "—";
                CfgPomoBreakText.Text = "—";
                CfgRotateText.Text = "—";
                CfgArtModeText.Text = "—";
                CfgBarModeText.Text = "—";
            }
            else
            {
                CfgRefreshText.Text = DeviceConfigService.RefreshText(config.Refresh);
                CfgPomoFocusText.Text = DeviceConfigService.MinutesText(config.PomoFocus);
                CfgPomoBreakText.Text = DeviceConfigService.MinutesText(config.PomoBreak);
                CfgRotateText.Text = DeviceConfigService.RotateText(config.Rotate);
                CfgArtModeText.Text = DeviceConfigService.ArtModeText(config.ArtMode);
                CfgBarModeText.Text = DeviceConfigService.BarModeText(config.BarMode);
            }

            CfgSyncText.Text = _services.DeviceConfig.LastSync is { } time
                ? $"最近同步：{time:HH:mm:ss}"
                : "最近同步：—";
        });
    }

    private void LoadSettings()
    {
        var settings = _services.Settings.Current;
        AutoDetectSwitch.IsOn = settings.AutoDetectPort;
        PushOnEventSwitch.IsOn = settings.PushOnEvent;
        PeriodicSwitch.IsOn = settings.PeriodicPushEnabled;
        PeriodicSecondsBox.Value = settings.PeriodicPushSeconds;
        PushTodoSwitch.IsOn = settings.PushTodo;
        AutoStartSwitch.IsOn = AutoStartService.IsEnabled();

        PushNewsSwitch.IsOn = settings.PushNews;
        NewsFeedUrlBox.Text = settings.NewsFeedUrl;
        NewsUrlBox.Text = settings.NewsUrl;
        NewsFileBox.Text = settings.NewsFile;
        NewsManualBox.Text = settings.NewsManual;
        NewsSourceCombo.SelectedIndex = settings.NewsSource switch
        {
            NewsSourceKind.Url => 1,
            NewsSourceKind.File => 2,
            NewsSourceKind.Manual => 3,
            _ => 0
        };

        UpdateNewsSourceVisibility();
    }

    private void RefreshPorts()
    {
        var ports = SerialLinkService.GetPortNames();
        PortCombo.ItemsSource = ports;

        var saved = _services.Settings.Current.PortName;
        if (!string.IsNullOrWhiteSpace(saved))
        {
            var match = ports.FirstOrDefault(p => string.Equals(p, saved, StringComparison.OrdinalIgnoreCase));
            if (match is not null)
            {
                PortCombo.SelectedItem = match;
            }
        }

        PortHintText.Text = ports.Length == 0
            ? "未发现 COM 口"
            : $"发现 {ports.Length} 个：{string.Join("、", ports)}";
    }

    private void RefreshPorts_Click(object sender, RoutedEventArgs e) => RefreshPorts();

    private void OpenPort_Click(object sender, RoutedEventArgs e)
    {
        if (PortCombo.SelectedItem is not string port)
        {
            SavedText.Text = "请先选择端口";
            return;
        }

        _services.Settings.Current.PortName = port;
        _services.Settings.Save();
        SavedText.Text = $"已打开 {port}";
    }

    private void Save_Click(object sender, RoutedEventArgs e)
    {
        var settings = _services.Settings.Current;

        settings.AutoDetectPort = AutoDetectSwitch.IsOn;
        settings.PushOnEvent = PushOnEventSwitch.IsOn;
        settings.PeriodicPushEnabled = PeriodicSwitch.IsOn;
        settings.PeriodicPushSeconds = double.IsNaN(PeriodicSecondsBox.Value)
            ? 60
            : (int)Math.Clamp(PeriodicSecondsBox.Value, 10, 3600);
        settings.PushTodo = PushTodoSwitch.IsOn;
        settings.AutoStart = AutoStartSwitch.IsOn;
        if (PortCombo.SelectedItem is string port)
        {
            settings.PortName = port;
        }

        ApplyNewsFields();

        _services.Settings.Save();
        AutoStartService.Set(settings.AutoStart);
        SavedText.Text = "已保存";
    }
}

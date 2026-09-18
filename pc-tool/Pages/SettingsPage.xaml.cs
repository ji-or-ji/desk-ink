using DeskInk.PcTool.Services;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

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

        _services.Settings.Save();
        AutoStartService.Set(settings.AutoStart);
        SavedText.Text = "已保存";
    }
}

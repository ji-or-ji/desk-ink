using System.Runtime.InteropServices;
using DeskInk.PcTool.Pages;
using DeskInk.PcTool.Services;
using Microsoft.UI;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using WinRT.Interop;

namespace DeskInk.PcTool;

public sealed partial class MainWindow : Window
{
    private const int SwHide = 0;
    private const int SwShow = 5;

    private readonly AppServices _services;
    private readonly IntPtr _hwnd;
    private bool _trayCreated;
    private bool _reallyExit;

    public MainWindow(AppServices services)
    {
        _services = services;
        InitializeComponent();

        _hwnd = WindowNative.GetWindowHandle(this);

        ContentFrame.Navigate(typeof(ManagePage));

        AppWindow.Closing += OnClosing;
        Activated += OnActivated;

        _services.Ipc.ConnectionChanged += _ => UpdateTray();
        _services.Serial.StateChanged += _ => UpdateTray();
        _services.Push.PushStateChanged += UpdateTray;
    }

    private void OnActivated(object sender, WindowActivatedEventArgs args)
    {
        if (_trayCreated)
        {
            return;
        }

        _trayCreated = true;
        try
        {
            TrayIcon.ForceCreate();
        }
        catch
        {
            // 托盘创建失败不影响主功能。
        }

        UpdateTray();
    }

    private void OnClosing(AppWindow sender, AppWindowClosingEventArgs args)
    {
        if (_reallyExit)
        {
            return;
        }

        // 关闭主窗口只缩到托盘，符合托盘工具的预期（pc-tool.md 5.3）。
        args.Cancel = true;
        ShowWindow(_hwnd, SwHide);
    }

    private void Nav_SelectionChanged(NavigationView sender, NavigationViewSelectionChangedEventArgs args)
    {
        if (args.SelectedItem is not NavigationViewItem item)
        {
            return;
        }

        var page = item.Tag as string == "settings" ? typeof(SettingsPage) : typeof(ManagePage);
        if (ContentFrame.CurrentSourcePageType != page)
        {
            ContentFrame.Navigate(page);
        }
    }

    private void TrayOpenManage_Click(object sender, RoutedEventArgs e) => ShowPage(0);

    private void TrayOpenSettings_Click(object sender, RoutedEventArgs e) => ShowPage(1);

    private void TrayExit_Click(object sender, RoutedEventArgs e)
    {
        _reallyExit = true;
        try
        {
            TrayIcon.Dispose();
        }
        catch
        {
            // 忽略托盘释放异常。
        }

        _services.Dispose();
        Close();
        Application.Current.Exit();
    }

    private void ShowPage(int index)
    {
        ShowWindow(_hwnd, SwShow);
        Activate();
        if (index >= 0 && index < Nav.MenuItems.Count)
        {
            Nav.SelectedItem = Nav.MenuItems[index];
        }
    }

    private void UpdateTray()
    {
        Ui.Post(() =>
        {
            if (_services.Serial.IsOpen)
            {
                if (_services.Ipc.IsConnected)
                {
                    SetTray("✓", Colors.SeaGreen, $"DeskInk 已就绪（{_services.Serial.PortName}）");
                }
                else
                {
                    SetTray("…", Colors.Goldenrod, "串口已连接，等待 ClassIsland");
                }
            }
            else
            {
                SetTray("!", Colors.OrangeRed, "串口未连接");
            }
        });
    }

    private void SetTray(string text, Windows.UI.Color color, string tooltip)
    {
        TrayIconSource.Text = text;
        TrayIconSource.Foreground = new SolidColorBrush(color);
        TrayIcon.ToolTipText = tooltip;
    }

    [DllImport("user32.dll")]
    private static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
}

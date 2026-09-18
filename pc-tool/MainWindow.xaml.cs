using System.Runtime.InteropServices;
using DeskInk.PcTool.Pages;
using DeskInk.PcTool.Services;
using Microsoft.UI;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Windows.Graphics;
using WinRT.Interop;

namespace DeskInk.PcTool;

public sealed partial class MainWindow : Window
{
    private const int SwHide = 0;
    private const int SwShow = 5;
    private const int SwRestore = 9;

    private readonly AppServices _services;
    private readonly IntPtr _hwnd;
    private bool _trayCreated;
    private bool _reallyExit;

    public MainWindow(AppServices services)
    {
        _services = services;
        InitializeComponent();

        _hwnd = WindowNative.GetWindowHandle(this);
        ResizeToWorkArea();

        ContentFrame.Navigate(typeof(ManagePage));

        AppWindow.Closing += OnClosing;
        Activated += OnActivated;

        // 左键单击托盘图标就把窗口唤回来（右键仍出菜单）。
        // 注意：H.NotifyIcon 默认的 PopupMenu 模式只认 MenuFlyoutItem.Command，不触发 Click 事件。
        TrayIcon.LeftClickCommand = new DelegateCommand(() => ShowPage("manage", "左键"));
        TrayManageItem.Command = new DelegateCommand(() => ShowPage("manage", "菜单：管理页"));
        TraySettingsItem.Command = new DelegateCommand(() => ShowPage("settings", "菜单：设置"));
        TrayArtworkItem.Command = new DelegateCommand(() => ShowPage("artwork", "菜单：插画"));
        TrayQuoteItem.Command = new DelegateCommand(() => ShowPage("quote", "菜单：一言"));
        TrayExitItem.Command = new DelegateCommand(ExitApplication);

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

    private void ResizeToWorkArea()
    {
        try
        {
            var workArea = DisplayArea.GetFromWindowId(AppWindow.Id, DisplayAreaFallback.Primary).WorkArea;
            AppWindow.Resize(new SizeInt32(Math.Min(1200, workArea.Width), Math.Min(800, workArea.Height)));
        }
        catch
        {
            // 调整窗口大小失败不影响功能。
        }
    }

    private void Nav_SelectionChanged(NavigationView sender, NavigationViewSelectionChangedEventArgs args)
    {
        if (args.SelectedItem is not NavigationViewItem item)
        {
            return;
        }

        var page = (item.Tag as string) switch
        {
            "settings" => typeof(SettingsPage),
            "artwork" => typeof(ArtworkPage),
            "quote" => typeof(QuotePage),
            _ => typeof(ManagePage)
        };

        if (ContentFrame.CurrentSourcePageType != page)
        {
            ContentFrame.Navigate(page);
        }
    }

    private void ExitApplication()
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

    private void ShowPage(string tag, string reason)
    {
        _services.Log.Info($"托盘唤起：{reason}");

        try
        {
            ShowWindow(_hwnd, SwShow);
            ShowWindow(_hwnd, SwRestore);
            SetForegroundWindow(_hwnd);
            Activate();
        }
        catch (Exception ex)
        {
            _services.Log.Warn($"唤回窗口失败：{ex.Message}");
        }

        var item = FindNavItem(tag);
        if (item is not null)
        {
            Nav.SelectedItem = item;
        }
    }

    private NavigationViewItem? FindNavItem(string tag)
    {
        foreach (var entry in Nav.MenuItems.Concat(Nav.FooterMenuItems))
        {
            if (entry is NavigationViewItem item && (item.Tag as string) == tag)
            {
                return item;
            }
        }

        return null;
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

    [DllImport("user32.dll")]
    private static extern bool SetForegroundWindow(IntPtr hWnd);
}

using DeskInk.PcTool.Services;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;

namespace DeskInk.PcTool;

public partial class App : Application
{
    private static Mutex? _instanceMutex;

    private readonly AppServices _services = new();

    public App()
    {
        // 托盘工具只允许一个实例，避免出现两个图标/两个窗口互相抢。
        _instanceMutex = new Mutex(initiallyOwned: true, @"Local\DeskInk.PcTool.SingleInstance", out var createdNew);
        if (!createdNew)
        {
            Environment.Exit(0);
            return;
        }

        InitializeComponent();
        Ui.Dispatcher = DispatcherQueue.GetForCurrentThread();
    }

    public static App Instance { get; private set; } = null!;

    public AppServices Services => _services;

    public MainWindow? MainWindow { get; private set; }

    protected override void OnLaunched(LaunchActivatedEventArgs args)
    {
        Instance = this;
        MainWindow = new MainWindow(_services);
        MainWindow.Activate();
        _services.Start();
    }
}

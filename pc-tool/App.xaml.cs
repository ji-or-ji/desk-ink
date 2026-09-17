using DeskInk.PcTool.Services;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;

namespace DeskInk.PcTool;

public partial class App : Application
{
    private readonly AppServices _services = new();

    public App()
    {
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

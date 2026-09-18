namespace DeskInk.PcTool.Services;

/// <summary>把各服务串在一起，供 App 与页面共享。</summary>
public sealed class AppServices : IDisposable
{
    public AppServices()
    {
        Settings = new SettingsService();
        Log = new LogService();
        Ipc = new ClassIslandIpcService(Log);
        Serial = new SerialLinkService(Log);
        Quote = new QuoteService(Settings, Log);
        Artwork = new ArtworkService(Serial, Log);
        DeviceConfig = new DeviceConfigService();
        Push = new PushCoordinator(Ipc, Serial, Settings, Quote, DeviceConfig, Log);
    }

    public SettingsService Settings { get; }

    public LogService Log { get; }

    public ClassIslandIpcService Ipc { get; }

    public SerialLinkService Serial { get; }

    public QuoteService Quote { get; }

    public ArtworkService Artwork { get; }

    public DeviceConfigService DeviceConfig { get; }

    /// <summary>设备端真字库，合成预览用；找不到则为 null。</summary>
    public DeviceFont? DeviceFont { get; private set; }

    public PushCoordinator Push { get; }

    private readonly object _serialLock = new();

    public void Start()
    {
        Settings.Changed += OnSettingsChanged;

        // 串口枚举/打开、字库读取都可能阻塞（蓝牙 COM 口尤其），一律放到后台线程，
        // 否则会卡在 UI 线程上——表现为「窗口出来了但整个点不动」。
        _ = Task.Run(() =>
        {
            ApplySerialSettings();
            LoadDeviceFont();
        });

        Ipc.Start();
        Quote.Start();
        Push.Start();
    }

    private void OnSettingsChanged() => _ = Task.Run(ApplySerialSettings);

    private void LoadDeviceFont()
    {
        try
        {
            DeviceFont = DeviceFont.TryLoadDefault();
            Log.Info(DeviceFont is null
                ? "未找到设备字库（Assets/font16_simsun.bin / ascii8x16.bin），合成预览不显示文字"
                : "已加载设备字库，合成预览可用");
        }
        catch (Exception ex)
        {
            DeviceFont = null;
            Log.Warn($"加载设备字库失败：{ex.Message}");
        }
    }

    /// <summary>按设置里的端口名/自动发现结果打开串口（可能阻塞，别在 UI 线程直接调）。</summary>
    public void ApplySerialSettings()
    {
        lock (_serialLock)
        {
            ApplySerialSettingsCore();
        }
    }

    private void ApplySerialSettingsCore()
    {
        var settings = Settings.Current;
        var ports = SerialLinkService.GetPortNames();

        if (!string.IsNullOrWhiteSpace(settings.PortName))
        {
            var match = ports.FirstOrDefault(p => string.Equals(p, settings.PortName, StringComparison.OrdinalIgnoreCase));
            if (match is not null)
            {
                if (!Serial.IsOpen || !string.Equals(Serial.PortName, match, StringComparison.OrdinalIgnoreCase))
                {
                    Serial.Open(match);
                }

                return;
            }

            Log.Warn($"设置的端口 {settings.PortName} 不存在，可用端口：{string.Join(", ", ports)}");
        }

        if (settings.AutoDetectPort)
        {
            if (ports.Length == 1)
            {
                if (!Serial.IsOpen || !string.Equals(Serial.PortName, ports[0], StringComparison.OrdinalIgnoreCase))
                {
                    Serial.Open(ports[0]);
                }

                return;
            }

            Log.Info(ports.Length == 0
                ? "未发现可用 COM 口，请在设置页手动指定"
                : $"发现 {ports.Length} 个 COM 口，请在设置页选择：{string.Join(", ", ports)}");
        }

        if (Serial.IsOpen)
        {
            Serial.Close();
        }
    }

    public void Dispose()
    {
        Push.Dispose();
        Ipc.Dispose();
        Quote.Dispose();
        Serial.Dispose();
    }
}

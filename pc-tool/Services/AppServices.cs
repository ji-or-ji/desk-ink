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
        Push = new PushCoordinator(Ipc, Serial, Settings, Quote, Log);
    }

    public SettingsService Settings { get; }

    public LogService Log { get; }

    public ClassIslandIpcService Ipc { get; }

    public SerialLinkService Serial { get; }

    public QuoteService Quote { get; }

    public ArtworkService Artwork { get; }

    public PushCoordinator Push { get; }

    public void Start()
    {
        Settings.Changed += ApplySerialSettings;
        ApplySerialSettings();

        Ipc.Start();
        Quote.Start();
        Push.Start();
    }

    /// <summary>按设置里的端口名/自动发现结果打开串口。</summary>
    public void ApplySerialSettings()
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

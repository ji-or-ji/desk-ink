using System.Text.Json;
using DeskInk.PcTool.Models;

namespace DeskInk.PcTool.Services;

/// <summary>
/// 订阅 ClassIsland 事件与设备消息，组包后写串口。
/// </summary>
public sealed class PushCoordinator : IDisposable
{
    private readonly ClassIslandIpcService _ipc;
    private readonly SerialLinkService _serial;
    private readonly SettingsService _settings;
    private readonly QuoteService _quote;
    private readonly LogService _log;

    private readonly CancellationTokenSource _cts = new();
    private Task? _periodic;

    private LessonSnapshot? _last;

    public PushCoordinator(
        ClassIslandIpcService ipc,
        SerialLinkService serial,
        SettingsService settings,
        QuoteService quote,
        LogService log)
    {
        _ipc = ipc;
        _serial = serial;
        _settings = settings;
        _quote = quote;
        _log = log;
    }

    public int PushCount { get; private set; }

    public DateTime? LastPushTime { get; private set; }

    public event Action? PushStateChanged;

    public void Start()
    {
        _ipc.LessonUpdated += OnLessonUpdated;
        _ipc.LessonEvent += OnLessonEvent;
        _ipc.ConnectionChanged += OnConnectionChanged;
        _serial.LineReceived += OnLineReceived;
        _quote.Changed += OnQuoteChanged;
        _periodic = Task.Run(() => PeriodicLoopAsync(_cts.Token));
    }

    private void OnLessonUpdated(LessonSnapshot snapshot) => _last = snapshot;

    private void OnLessonEvent(string name)
    {
        if (!_settings.Current.PushOnEvent)
        {
            return;
        }

        PushSchedule($"事件：{name}");
    }

    private void OnConnectionChanged(bool connected)
    {
        if (connected)
        {
            PushAll("ClassIsland 已连接");
        }
    }

    private void OnQuoteChanged()
    {
        if (_serial.IsOpen)
        {
            PushQuote("每日一言更新");
        }
    }

    private void OnLineReceived(string line)
    {
        string? type = null;
        try
        {
            using var document = JsonDocument.Parse(line);
            var root = document.RootElement;
            if (root.ValueKind == JsonValueKind.Object)
            {
                if (root.TryGetProperty("t", out var t) && t.ValueKind == JsonValueKind.String)
                {
                    type = t.GetString();
                }
                else if (root.TryGetProperty("type", out var alt) && alt.ValueKind == JsonValueKind.String)
                {
                    type = alt.GetString();
                }
            }
        }
        catch (JsonException)
        {
            _log.Warn($"无法解析设备消息：{line}");
            return;
        }

        switch (type)
        {
            case "hello":
                _log.Info($"设备握手：{line}");
                PushAll("设备上电 hello");
                break;
            case "req":
                _log.Info("设备请求刷新");
                PushAll("设备 req");
                break;
            case "status":
                _log.Info($"设备状态：{line}");
                break;
            case "ack":
                _log.Info($"设备确认：{line}");
                break;
            default:
                _log.Warn($"收到未知消息：{line}");
                break;
        }
    }

    public bool PushSchedule(string reason)
    {
        var snapshot = _last ?? _ipc.LastSnapshot;
        if (snapshot is null)
        {
            _log.Warn($"暂无课表数据，跳过推送（{reason}）");
            return false;
        }

        return Send(ProtocolBuilder.BuildSchedule(snapshot), $"schedule（{reason}）");
    }

    public bool PushQuote(string reason)
    {
        if (!_settings.Current.PushQuote)
        {
            return false;
        }

        var quote = _quote.Current;
        if (string.IsNullOrWhiteSpace(quote))
        {
            return false;
        }

        return Send(ProtocolBuilder.BuildQuote(quote), $"quote（{reason}）");
    }

    public bool PushTodo(string reason)
    {
        if (_settings.Current.Todos.Count == 0)
        {
            return false;
        }

        return Send(ProtocolBuilder.BuildTodo(_settings.Current.Todos), $"todo（{reason}）");
    }

    public void PushAll(string reason)
    {
        PushSchedule(reason);
        PushQuote(reason);
        if (_settings.Current.PushTodo)
        {
            PushTodo(reason);
        }
    }

    private bool Send(string line, string label)
    {
        if (!_serial.SendLine(line))
        {
            return false;
        }

        PushCount++;
        LastPushTime = DateTime.Now;
        _log.Push($"{label} → {line}");
        PushStateChanged?.Invoke();
        return true;
    }

    private async Task PeriodicLoopAsync(CancellationToken token)
    {
        while (!token.IsCancellationRequested)
        {
            var seconds = Math.Clamp(_settings.Current.PeriodicPushSeconds, 10, 3600);
            try
            {
                await Task.Delay(TimeSpan.FromSeconds(seconds), token).ConfigureAwait(false);
            }
            catch (OperationCanceledException)
            {
                break;
            }

            if (_settings.Current.PeriodicPushEnabled)
            {
                PushSchedule("定时补推");
            }
        }
    }

    public void Dispose()
    {
        _cts.Cancel();
        try
        {
            _periodic?.Wait(TimeSpan.FromSeconds(1));
        }
        catch
        {
            // 忽略退出等待异常。
        }

        _cts.Dispose();
    }
}

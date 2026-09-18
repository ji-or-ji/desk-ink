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
    private readonly NewsService _news;
    private readonly DeviceConfigService _deviceConfig;
    private readonly LogService _log;

    private readonly CancellationTokenSource _cts = new();
    private Task? _periodic;

    private LessonSnapshot? _last;

    public PushCoordinator(
        ClassIslandIpcService ipc,
        SerialLinkService serial,
        SettingsService settings,
        QuoteService quote,
        NewsService news,
        DeviceConfigService deviceConfig,
        LogService log)
    {
        _ipc = ipc;
        _serial = serial;
        _settings = settings;
        _quote = quote;
        _news = news;
        _deviceConfig = deviceConfig;
        _log = log;
    }

    public int PushCount { get; private set; }

    public DateTime? LastPushTime { get; private set; }

    public event Action? PushStateChanged;

    /// <summary>设备回传待办并整体覆盖本地后触发，供待办页刷新列表。</summary>
    public event Action? TodoUpdated;

    // 设备回传待办的分片重组状态
    private readonly List<TodoItem> _todoBuffer = new();
    private int _todoTotalChunks;
    private int _todoExpectedSeq;

    public void Start()
    {
        _ipc.LessonUpdated += OnLessonUpdated;
        _ipc.LessonEvent += OnLessonEvent;
        _ipc.ConnectionChanged += OnConnectionChanged;
        _serial.LineReceived += OnLineReceived;
        _serial.StateChanged += OnSerialStateChanged;
        _quote.Changed += OnQuoteChanged;
        _news.Changed += OnNewsChanged;
        _periodic = Task.Run(() => PeriodicLoopAsync(_cts.Token));
    }

    private void OnLessonUpdated(LessonSnapshot snapshot) => _last = snapshot;

    private void OnLessonEvent(string name)
    {
        if (!_settings.Current.PushOnEvent)
        {
            return;
        }

        PushScheduleWithTodo($"事件：{name}");
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

    private void OnSerialStateChanged(bool connected)
    {
        if (connected)
        {
            // 串口一连上就把当前新闻推一次（设备是被动接收方，不主动索取）。
            PushNews("串口连接");
            return;
        }

        // 设备断了就清掉配置镜像，界面显示「—」，不要拿旧值冒充。
        _deviceConfig.Clear();
    }

    private void OnNewsChanged()
    {
        if (_serial.IsOpen)
        {
            PushNews("新闻更新");
        }
    }

    public bool PushNews(string reason)
    {
        var text = _news.Current;
        if (string.IsNullOrWhiteSpace(text))
        {
            return false;
        }

        // title 留空，正文进 body（设备端 body 自动换行）。
        return Send(ProtocolBuilder.BuildNews(text), $"news（{reason}）");
    }

    private void HandleConfig(string line)
    {
        try
        {
            var message = JsonSerializer.Deserialize<ConfigMessage>(line);
            if (message is null)
            {
                return;
            }

            _deviceConfig.Update(new DeviceConfig(
                message.Refresh,
                message.PomoFocus,
                message.PomoBreak,
                message.Rotate,
                message.ArtMode,
                message.BarMode));

            _log.Info(
                $"设备配置：刷新={DeviceConfigService.RefreshText(message.Refresh)}" +
                $" 专注={message.PomoFocus} 分 休息={message.PomoBreak} 分" +
                $" 轮换={DeviceConfigService.RotateText(message.Rotate)}" +
                $" 插画={DeviceConfigService.ArtModeText(message.ArtMode)}" +
                $" 信息条={DeviceConfigService.BarModeText(message.BarMode)}");
        }
        catch (JsonException ex)
        {
            _log.Warn($"解析设备配置失败：{ex.Message}");
        }
    }

    /// <summary>接收设备回传的待办（分片、全量语义）：按 seq/total 重组后整体覆盖本地。</summary>
    private void HandleTodo(string line)
    {
        TodoMessage? message;
        try
        {
            message = JsonSerializer.Deserialize<TodoMessage>(line);
        }
        catch (JsonException ex)
        {
            _log.Warn($"解析设备待办失败：{ex.Message}");
            return;
        }

        if (message is null)
        {
            return;
        }

        if (message.Seq == 0)
        {
            // 新的一轮，清空重来。
            _todoBuffer.Clear();
            _todoTotalChunks = message.Total;
            _todoExpectedSeq = 0;
        }
        else if (message.Total != _todoTotalChunks || message.Seq != _todoExpectedSeq)
        {
            _log.Warn($"待办分片跳号（seq={message.Seq}，期望 {_todoExpectedSeq}），丢弃整轮");
            _todoBuffer.Clear();
            _todoTotalChunks = 0;
            _todoExpectedSeq = 0;
            return;
        }

        _todoBuffer.AddRange(message.Items);
        _todoExpectedSeq++;

        if (_todoTotalChunks <= 0 || _todoExpectedSeq < _todoTotalChunks)
        {
            return;
        }

        var items = _todoBuffer.Take(8).ToList();
        _todoBuffer.Clear();
        _todoTotalChunks = 0;
        _todoExpectedSeq = 0;

        _settings.Current.Todos = items;
        _settings.Save();

        _log.Info($"设备回传待办：{items.Count} 条，已整体覆盖本地");
        TodoUpdated?.Invoke();
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
            case "todo":
                HandleTodo(line);
                break;
            case "cfg":
                HandleConfig(line);
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
        var items = _settings.Current.Todos;
        if (items.Count == 0)
        {
            return false;
        }

        _ = SendTodoChunksAsync(items.ToList(), reason);
        return true;
    }

    /// <summary>待办必须分片（每片 2 条，片间 20ms），单帧超 255 会被设备丢弃。</summary>
    private async Task SendTodoChunksAsync(IReadOnlyList<TodoItem> items, string reason)
    {
        var chunks = ProtocolBuilder.BuildTodoChunks(items);
        for (var i = 0; i < chunks.Count; i++)
        {
            Send(chunks[i], $"todo {i + 1}/{chunks.Count}（{reason}）");

            if (i < chunks.Count - 1)
            {
                try
                {
                    await Task.Delay(20).ConfigureAwait(false);
                }
                catch
                {
                    // 忽略取消。
                }
            }
        }
    }

    /// <summary>随课表一起推待办（仅当「随课表推送待办」开关打开）。</summary>
    private void PushScheduleWithTodo(string reason)
    {
        PushSchedule(reason);
        if (_settings.Current.PushTodo)
        {
            PushTodo(reason);
        }

        if (_settings.Current.PushNews)
        {
            PushNews(reason);
        }
    }

    /// <summary>全量推送（连接、hello、req）。按硬规则：**不含待办**，避免覆盖设备上刚勾的。</summary>
    public void PushAll(string reason)
    {
        PushSchedule(reason);
        PushQuote(reason);
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
                PushScheduleWithTodo("定时补推");
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

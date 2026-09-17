using ClassIsland.Shared.Enums;
using ClassIsland.Shared.IPC;
using ClassIsland.Shared.IPC.Abstractions.Services;
using DeskInk.PcTool.Models;
using dotnetCampus.Ipc.CompilerServices.GeneratedProxies;

namespace DeskInk.PcTool.Services;

/// <summary>
/// 通过 ClassIsland.Shared.Ipc 读取课表状态。连不上时按固定间隔重试。
/// </summary>
public sealed class ClassIslandIpcService : IDisposable
{
    private static readonly TimeSpan ConnectTimeout = TimeSpan.FromSeconds(10);
    private static readonly TimeSpan RetryDelay = TimeSpan.FromSeconds(5);
    private static readonly TimeSpan PollInterval = TimeSpan.FromSeconds(1);

    private readonly LogService _log;
    private readonly CancellationTokenSource _cts = new();
    private Task? _loop;

    private IpcClient? _client;
    private IPublicLessonsService? _lessons;

    public ClassIslandIpcService(LogService log)
    {
        _log = log;
    }

    public bool IsConnected { get; private set; }

    public LessonSnapshot? LastSnapshot { get; private set; }

    public event Action<bool>? ConnectionChanged;

    public event Action<LessonSnapshot>? LessonUpdated;

    /// <summary>上课 / 课间 / 放学 / 时间点变化，参数是中文事件名，仅用于日志。</summary>
    public event Action<string>? LessonEvent;

    public void Start()
    {
        if (_loop is not null)
        {
            return;
        }

        _loop = Task.Run(() => RunAsync(_cts.Token));
    }

    private async Task RunAsync(CancellationToken token)
    {
        _log.Info("正在连接 ClassIsland（IPC）…");

        while (!token.IsCancellationRequested)
        {
            IpcClient? client = null;
            try
            {
                client = new IpcClient();
                await client.Connect().WaitAsync(ConnectTimeout, token).ConfigureAwait(false);

                var lessons = client.Provider.CreateIpcProxy<IPublicLessonsService>(client.PeerProxy!);

                client.JsonIpcProvider.AddNotifyHandler(IpcRoutedNotifyIds.OnClassNotifyId, () => RaiseLessonEvent("上课"));
                client.JsonIpcProvider.AddNotifyHandler(IpcRoutedNotifyIds.OnBreakingTimeNotifyId, () => RaiseLessonEvent("课间"));
                client.JsonIpcProvider.AddNotifyHandler(IpcRoutedNotifyIds.OnAfterSchoolNotifyId, () => RaiseLessonEvent("放学"));
                client.JsonIpcProvider.AddNotifyHandler(IpcRoutedNotifyIds.CurrentTimeStateChangedNotifyId, () => RaiseLessonEvent("时间点变化"));

                _client = client;
                _lessons = lessons;
                SetConnected(true);
                _log.Info("已连接 ClassIsland");

                await PollAsync(token).ConfigureAwait(false);
            }
            catch (OperationCanceledException)
            {
                // 正常退出。
            }
            catch (Exception ex)
            {
                _log.Warn($"ClassIsland 连接不可用：{ex.Message}");
            }
            finally
            {
                _lessons = null;
                _client = null;
                try
                {
                    client?.Provider?.Dispose();
                }
                catch
                {
                    // 忽略释放异常。
                }

                SetConnected(false);
            }

            if (token.IsCancellationRequested)
            {
                break;
            }

            try
            {
                await Task.Delay(RetryDelay, token).ConfigureAwait(false);
            }
            catch (OperationCanceledException)
            {
                break;
            }
        }
    }

    private async Task PollAsync(CancellationToken token)
    {
        while (!token.IsCancellationRequested)
        {
            var snapshot = ReadSnapshot();
            LastSnapshot = snapshot;
            LessonUpdated?.Invoke(snapshot);
            await Task.Delay(PollInterval, token).ConfigureAwait(false);
        }
    }

    private LessonSnapshot ReadSnapshot()
    {
        var lessons = _lessons ?? throw new InvalidOperationException("IPC 未连接。");

        var state = lessons.CurrentState;
        var current = lessons.CurrentSubject;
        var currentItem = lessons.CurrentTimeLayoutItem;
        var nextSubject = lessons.NextClassSubject;
        var nextItem = lessons.NextClassTimeLayoutItem;

        var name = current?.Name ?? string.Empty;
        var initial = current?.Initial ?? string.Empty;
        if (string.IsNullOrEmpty(initial) && current is not null)
        {
            try
            {
                initial = current.GetFirstName();
            }
            catch
            {
                // 忽略取简称失败。
            }
        }

        if (string.IsNullOrEmpty(initial) && name.Length > 0)
        {
            initial = name[..1];
        }

        var hasCurrent = state != TimeState.None;
        var left = state switch
        {
            TimeState.OnClass => lessons.OnBreakingTimeLeftTime,
            TimeState.Breaking => lessons.OnClassLeftTime,
            _ => TimeSpan.Zero
        };

        return new LessonSnapshot(
            state,
            name,
            initial,
            hasCurrent ? FormatTime(currentItem?.StartTime) : string.Empty,
            hasCurrent ? FormatTime(currentItem?.EndTime) : string.Empty,
            left <= TimeSpan.Zero ? 0 : (int)Math.Ceiling(left.TotalMinutes),
            nextSubject?.Name ?? string.Empty,
            FormatTime(nextItem?.StartTime),
            FormatTime(nextItem?.EndTime));
    }

    private static string FormatTime(TimeSpan? value)
        => value is null || value.Value == TimeSpan.Zero ? string.Empty : value.Value.ToString(@"hh\:mm");

    private void RaiseLessonEvent(string name)
    {
        _log.Info($"ClassIsland 事件：{name}");
        LessonEvent?.Invoke(name);
    }

    private void SetConnected(bool value)
    {
        if (IsConnected == value)
        {
            return;
        }

        IsConnected = value;
        ConnectionChanged?.Invoke(value);
    }

    public void Dispose()
    {
        _cts.Cancel();
        try
        {
            _loop?.Wait(TimeSpan.FromSeconds(2));
        }
        catch
        {
            // 忽略退出等待异常。
        }

        try
        {
            _client?.Provider?.Dispose();
        }
        catch
        {
            // 忽略释放异常。
        }

        _cts.Dispose();
    }
}

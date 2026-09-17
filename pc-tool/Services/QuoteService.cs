using System.Text.Json;
using DeskInk.PcTool.Models;

namespace DeskInk.PcTool.Services;

/// <summary>
/// 每日一言：默认从公益源（一言 hitokoto）取，也支持手动填写 / 自定义网址 / 本地文件。
/// 每天取一条（跨天自动刷新），设置页可手动「立即获取」。
/// </summary>
public sealed class QuoteService : IDisposable
{
    private const string HitokotoUrl = "https://v1.hitokoto.cn/";

    private static readonly HttpClient Http = new() { Timeout = TimeSpan.FromSeconds(10) };

    /// <summary>联网源不可用时的兜底文案（离线也能推）。</summary>
    private static readonly string[] BuiltInQuotes =
    {
        "把一件事做完整，比做十件半成品更有价值。",
        "慢慢来，比较快。",
        "今天多学一点，明天就少慌一点。",
        "先把书读厚，再把书读薄。",
        "认真做完眼前这一题，就是进步。",
        "休息也是学习的一部分。",
        "不为模糊的未来担忧，只为清楚的当下努力。"
    };

    private readonly SettingsService _settings;
    private readonly LogService _log;
    private readonly CancellationTokenSource _cts = new();

    private Task? _daily;
    private DateOnly? _fetchedDate;

    public QuoteService(SettingsService settings, LogService log)
    {
        _settings = settings;
        _log = log;
    }

    /// <summary>当前解析出来的文案，空串表示还没取到。</summary>
    public string Current { get; private set; } = string.Empty;

    /// <summary>当前来源的可读描述，用于界面与日志。</summary>
    public string SourceText { get; private set; } = "尚未获取";

    public event Action? Changed;

    public void Start()
    {
        _ = RefreshAsync("启动");
        _daily = Task.Run(() => DailyLoopAsync(_cts.Token));
    }

    public async Task<bool> RefreshAsync(string reason)
    {
        string? text = null;
        try
        {
            text = await ResolveAsync().ConfigureAwait(false);
        }
        catch (Exception ex)
        {
            _log.Warn($"获取每日一言失败（{reason}）：{ex.Message}");
        }

        if (string.IsNullOrWhiteSpace(text))
        {
            if (!string.IsNullOrEmpty(Current))
            {
                _log.Warn($"每日一言未更新（{reason}），沿用上一条");
                return false;
            }

            SourceText = "内置备用";
            Current = FallbackQuote();
            _log.Warn($"每日一言源不可用（{reason}），先用内置备用：{Current}");
            Changed?.Invoke();
            return false;
        }

        Current = text.Trim();
        _fetchedDate = DateOnly.FromDateTime(DateTime.Now);
        _log.Info($"每日一言（{reason}·{SourceText}）：{Current}");
        Changed?.Invoke();
        return true;
    }

    private static string FallbackQuote() => BuiltInQuotes[DateTime.Now.DayOfYear % BuiltInQuotes.Length];

    private async Task<string?> ResolveAsync()
    {
        var settings = _settings.Current;
        switch (settings.QuoteSource)
        {
            case QuoteSourceKind.Hitokoto:
                SourceText = "公益源（一言）";
                return await FetchHitokotoAsync().ConfigureAwait(false);

            case QuoteSourceKind.Url:
                SourceText = string.IsNullOrWhiteSpace(settings.QuoteUrl) ? "自定义网址（未填写）" : $"网址 {settings.QuoteUrl}";
                return await FetchUrlAsync(settings.QuoteUrl).ConfigureAwait(false);

            case QuoteSourceKind.File:
                SourceText = string.IsNullOrWhiteSpace(settings.QuoteFile) ? "本地文件（未填写）" : $"文件 {settings.QuoteFile}";
                return ReadFromFile(settings.QuoteFile);

            default:
                SourceText = "手动填写";
                return settings.Quote;
        }
    }

    private static async Task<string?> FetchHitokotoAsync()
    {
        var body = await Http.GetStringAsync(HitokotoUrl).ConfigureAwait(false);
        using var document = JsonDocument.Parse(body);
        return document.RootElement.TryGetProperty("hitokoto", out var value) ? value.GetString() : null;
    }

    private static async Task<string?> FetchUrlAsync(string url)
    {
        if (string.IsNullOrWhiteSpace(url))
        {
            return null;
        }

        var body = await Http.GetStringAsync(url).ConfigureAwait(false);
        var trimmed = body.Trim();

        if (trimmed.StartsWith("{", StringComparison.Ordinal))
        {
            try
            {
                using var document = JsonDocument.Parse(trimmed);
                foreach (var name in new[] { "hitokoto", "text", "content", "data", "quote" })
                {
                    if (document.RootElement.TryGetProperty(name, out var value) && value.ValueKind == JsonValueKind.String)
                    {
                        return value.GetString();
                    }
                }
            }
            catch (JsonException)
            {
                // 不是合法 JSON，退回按纯文本处理。
            }
        }

        return trimmed.Split('\n').Select(line => line.Trim()).FirstOrDefault(line => line.Length > 0);
    }

    private string? ReadFromFile(string path)
    {
        if (string.IsNullOrWhiteSpace(path) || !File.Exists(path))
        {
            return null;
        }

        var lines = File.ReadAllLines(path)
            .Select(line => line.Trim())
            .Where(line => line.Length > 0)
            .ToArray();

        if (lines.Length == 0)
        {
            return null;
        }

        // 按天固定取一条，保证「每日」稳定。
        return lines[DateTime.Now.DayOfYear % lines.Length];
    }

    private async Task DailyLoopAsync(CancellationToken token)
    {
        while (!token.IsCancellationRequested)
        {
            try
            {
                await Task.Delay(TimeSpan.FromMinutes(30), token).ConfigureAwait(false);
            }
            catch (OperationCanceledException)
            {
                break;
            }

            if (_fetchedDate != DateOnly.FromDateTime(DateTime.Now))
            {
                await RefreshAsync("跨天");
            }
        }
    }

    public void Dispose()
    {
        _cts.Cancel();
        try
        {
            _daily?.Wait(TimeSpan.FromSeconds(1));
        }
        catch
        {
            // 忽略退出等待异常。
        }

        _cts.Dispose();
    }
}

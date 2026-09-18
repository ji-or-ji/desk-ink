using System.Text.Json;
using System.Xml.Linq;
using DeskInk.PcTool.Models;

namespace DeskInk.PcTool.Services;

/// <summary>
/// 新闻：默认取 RSS/Atom 最新一条标题，也支持 JSON 网址 / 本地文件 / 手动填写。
/// 设备大部分时间脱离电脑，屏上显示的永远是「上次推到的那条」——这是架构使然，不是实时新闻。
/// </summary>
public sealed class NewsService
{
    private static readonly HttpClient Http = new() { Timeout = TimeSpan.FromSeconds(10) };

    private readonly SettingsService _settings;
    private readonly LogService _log;

    public NewsService(SettingsService settings, LogService log)
    {
        _settings = settings;
        _log = log;
    }

    public string Current { get; private set; } = string.Empty;

    public string SourceText { get; private set; } = "尚未获取";

    public event Action? Changed;

    public void Start() => _ = RefreshAsync("启动");

    public async Task<bool> RefreshAsync(string reason)
    {
        string? text = null;
        try
        {
            text = await ResolveAsync().ConfigureAwait(false);
        }
        catch (Exception ex)
        {
            _log.Warn($"获取新闻失败（{reason}）：{ex.Message}");
        }

        if (string.IsNullOrWhiteSpace(text))
        {
            _log.Warn($"新闻为空（{reason}），来源：{SourceText}");
            return false;
        }

        Current = ProtocolBuilder.TruncateNewsBody(text.Trim());
        _log.Info($"新闻（{reason}·{SourceText}）：{Current}");
        Changed?.Invoke();
        return true;
    }

    private async Task<string?> ResolveAsync()
    {
        var settings = _settings.Current;
        switch (settings.NewsSource)
        {
            case NewsSourceKind.Rss:
                SourceText = string.IsNullOrWhiteSpace(settings.NewsFeedUrl) ? "RSS（未填写）" : $"RSS {settings.NewsFeedUrl}";
                return await FetchFeedAsync(settings.NewsFeedUrl).ConfigureAwait(false);

            case NewsSourceKind.Url:
                SourceText = string.IsNullOrWhiteSpace(settings.NewsUrl) ? "网址（未填写）" : $"网址 {settings.NewsUrl}";
                return await FetchUrlAsync(settings.NewsUrl).ConfigureAwait(false);

            case NewsSourceKind.File:
                SourceText = string.IsNullOrWhiteSpace(settings.NewsFile) ? "本地文件（未填写）" : $"文件 {settings.NewsFile}";
                return ReadFromFile(settings.NewsFile);

            default:
                SourceText = "手动填写";
                return settings.NewsManual;
        }
    }

    private static async Task<string?> FetchFeedAsync(string url)
    {
        if (string.IsNullOrWhiteSpace(url))
        {
            return null;
        }

        var body = await Http.GetStringAsync(url).ConfigureAwait(false);
        var document = XDocument.Parse(body);
        var item = document.Descendants().FirstOrDefault(e => e.Name.LocalName is "item" or "entry");
        var title = item?.Elements().FirstOrDefault(e => e.Name.LocalName == "title")?.Value;
        return StripSourceSuffix(title);
    }

    /// <summary>RSS 标题常见的「 - 来源名」后缀剥掉，界面更干净。</summary>
    private static string? StripSourceSuffix(string? title)
    {
        if (string.IsNullOrWhiteSpace(title))
        {
            return title;
        }

        var trimmed = title.Trim();
        foreach (var separator in new[] { " - ", " | ", " — " })
        {
            var index = trimmed.LastIndexOf(separator, StringComparison.Ordinal);
            if (index > 0)
            {
                return trimmed[..index].Trim();
            }
        }

        return trimmed;
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
                foreach (var name in new[] { "title", "headline", "text", "content", "body", "data", "news" })
                {
                    if (document.RootElement.TryGetProperty(name, out var value) && value.ValueKind == JsonValueKind.String)
                    {
                        return value.GetString();
                    }
                }
            }
            catch (JsonException)
            {
                // 退回按纯文本处理。
            }
        }

        return trimmed.Split('\n').Select(line => line.Trim()).FirstOrDefault(line => line.Length > 0);
    }

    private static string? ReadFromFile(string path)
    {
        if (string.IsNullOrWhiteSpace(path) || !File.Exists(path))
        {
            return null;
        }

        var lines = File.ReadAllLines(path)
            .Select(line => line.Trim())
            .Where(line => line.Length > 0)
            .ToArray();

        return lines.Length == 0 ? null : lines[DateTime.Now.DayOfYear % lines.Length];
    }
}

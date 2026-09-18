using System.Text.Encodings.Web;
using System.Text.Json;
using DeskInk.PcTool.Models;

namespace DeskInk.PcTool.Services;

public sealed class AppSettings
{
    public string? PortName { get; set; }

    public bool AutoDetectPort { get; set; } = true;

    public bool PushOnEvent { get; set; } = true;

    public bool PeriodicPushEnabled { get; set; }

    public int PeriodicPushSeconds { get; set; } = 60;

    public bool PushTodo { get; set; }

    public bool AutoStart { get; set; }

    /// <summary>启动时是否直接最小化到托盘（自启场景默认开）。</summary>
    public bool StartMinimizedToTray { get; set; } = true;

    public bool PushQuote { get; set; } = true;

    public QuoteSourceKind QuoteSource { get; set; } = QuoteSourceKind.Hitokoto;

    /// <summary>手动填写的内容（QuoteSource = Manual 时使用）。</summary>
    public string Quote { get; set; } = string.Empty;

    /// <summary>自定义网址（QuoteSource = Url 时使用）。</summary>
    public string QuoteUrl { get; set; } = "https://v1.hitokoto.cn/";

    /// <summary>本地文件路径（QuoteSource = File 时使用）。</summary>
    public string QuoteFile { get; set; } = string.Empty;

    public bool PushNews { get; set; }

    public NewsSourceKind NewsSource { get; set; } = NewsSourceKind.Rss;

    /// <summary>RSS / Atom feed 网址。</summary>
    public string NewsFeedUrl { get; set; } = string.Empty;

    /// <summary>自定义网址（JSON 或纯文本）。</summary>
    public string NewsUrl { get; set; } = string.Empty;

    /// <summary>本地文件路径。</summary>
    public string NewsFile { get; set; } = string.Empty;

    /// <summary>手动填写的内容。</summary>
    public string NewsManual { get; set; } = string.Empty;

    /// <summary>上次选择的插画目标尺寸，下次默认沿用。</summary>
    public ArtworkModeKind ArtworkMode { get; set; } = ArtworkModeKind.FullScreen;

    public List<TodoItem> Todos { get; set; } = new();
}

public sealed class SettingsService
{
    private static readonly JsonSerializerOptions Options = new()
    {
        WriteIndented = true,
        Encoder = JavaScriptEncoder.UnsafeRelaxedJsonEscaping
    };

    public AppSettings Current { get; private set; } = new();

    public string FilePath { get; }

    public event Action? Changed;

    public SettingsService()
    {
        var dir = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "DeskInk");
        Directory.CreateDirectory(dir);
        FilePath = Path.Combine(dir, "settings.json");
        Load();
    }

    public void Load()
    {
        try
        {
            if (File.Exists(FilePath))
            {
                var json = File.ReadAllText(FilePath);
                Current = JsonSerializer.Deserialize<AppSettings>(json, Options) ?? new AppSettings();
            }
        }
        catch
        {
            Current = new AppSettings();
        }
    }

    public void Save()
    {
        try
        {
            File.WriteAllText(FilePath, JsonSerializer.Serialize(Current, Options));
        }
        catch
        {
            // 设置写失败不致命，保持内存值即可。
        }

        Changed?.Invoke();
    }
}

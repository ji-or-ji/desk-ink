using System.Text.Json.Serialization;
using ClassIsland.Shared.Enums;

namespace DeskInk.PcTool.Models;

public enum LogLevel
{
    Info,
    Push,
    Warn,
    Error
}

public sealed record LogEntry(DateTime Time, LogLevel Level, string Message)
{
    public string TimeText => Time.ToString("HH:mm:ss");

    public string LevelText => Level switch
    {
        LogLevel.Push => "推送",
        LogLevel.Warn => "警告",
        LogLevel.Error => "错误",
        _ => "信息"
    };
}

/// <summary>从 ClassIsland 读到的一帧课表状态（界面与协议共用）。</summary>
public sealed record LessonSnapshot(
    TimeState State,
    string CurrentName,
    string CurrentInitial,
    string CurrentStart,
    string CurrentEnd,
    int LeftMinutes,
    string NextName,
    string NextStart,
    string NextEnd)
{
    public static LessonSnapshot Empty { get; } =
        new(TimeState.None, string.Empty, string.Empty, string.Empty, string.Empty, 0, string.Empty, string.Empty, string.Empty);

    /// <summary>对应 protocol.md 的 state 字段：0 无 / 1 上课 / 2 准备上课 / 3 课间 / 4 放学。</summary>
    public int StateCode => (int)State;

    public string StateText => State switch
    {
        TimeState.OnClass => "上课",
        TimeState.PrepareOnClass => "准备上课",
        TimeState.Breaking => "课间",
        TimeState.AfterSchool => "放学",
        _ => "无"
    };

    public string CurrentTimeRange => string.IsNullOrEmpty(CurrentStart) ? "—" : $"{CurrentStart}–{CurrentEnd}";

    public string NextTimeRange => string.IsNullOrEmpty(NextStart) ? "—" : $"{NextStart}–{NextEnd}";

    public string LeftText => State is TimeState.OnClass or TimeState.Breaking ? $"{LeftMinutes} 分钟" : "—";
}

/// <summary>每日一言的来源。</summary>
public enum QuoteSourceKind
{
    /// <summary>公益源：一言 hitokoto。</summary>
    Hitokoto,

    /// <summary>手动填写。</summary>
    Manual,

    /// <summary>自定义网址。</summary>
    Url,

    /// <summary>本地文件（每行一条）。</summary>
    File
}

public sealed class TodoItem
{
    [JsonPropertyName("id")] public int Id { get; set; }
    [JsonPropertyName("done")] public bool Done { get; set; }
    [JsonPropertyName("text")] public string Text { get; set; } = string.Empty;
}

public sealed class ScheduleMessage
{
    [JsonPropertyName("t")] public string T { get; set; } = "schedule";
    [JsonPropertyName("state")] public int State { get; set; }
    [JsonPropertyName("cur")] public ScheduleCurrent? Cur { get; set; }
    [JsonPropertyName("next")] public ScheduleNext? Next { get; set; }
}

public sealed class ScheduleCurrent
{
    [JsonPropertyName("name")] public string Name { get; set; } = string.Empty;
    [JsonPropertyName("initial")] public string Initial { get; set; } = string.Empty;
    [JsonPropertyName("start")] public string Start { get; set; } = string.Empty;
    [JsonPropertyName("end")] public string End { get; set; } = string.Empty;
    [JsonPropertyName("left")] public int Left { get; set; }
}

public sealed class ScheduleNext
{
    [JsonPropertyName("name")] public string Name { get; set; } = string.Empty;
    [JsonPropertyName("start")] public string Start { get; set; } = string.Empty;
    [JsonPropertyName("end")] public string End { get; set; } = string.Empty;
}

public sealed class QuoteMessage
{
    [JsonPropertyName("t")] public string T { get; set; } = "quote";
    [JsonPropertyName("text")] public string Text { get; set; } = string.Empty;
}

public sealed class TodoMessage
{
    [JsonPropertyName("t")] public string T { get; set; } = "todo";
    [JsonPropertyName("items")] public List<TodoItem> Items { get; set; } = new();
}

public sealed class ImgBeginMessage
{
    [JsonPropertyName("t")] public string T { get; set; } = "img_begin";
    [JsonPropertyName("id")] public int Id { get; set; }
    [JsonPropertyName("name")] public string Name { get; set; } = string.Empty;
    [JsonPropertyName("bytes")] public int Bytes { get; set; }
    [JsonPropertyName("total")] public int Total { get; set; }
}

public sealed class ImgChunkMessage
{
    [JsonPropertyName("t")] public string T { get; set; } = "img_chunk";
    [JsonPropertyName("id")] public int Id { get; set; }
    [JsonPropertyName("seq")] public int Seq { get; set; }
    [JsonPropertyName("data")] public string Data { get; set; } = string.Empty;
}

public sealed class ImgEndMessage
{
    [JsonPropertyName("t")] public string T { get; set; } = "img_end";
    [JsonPropertyName("id")] public int Id { get; set; }
    [JsonPropertyName("crc")] public int Crc { get; set; }
}

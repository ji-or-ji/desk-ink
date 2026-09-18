using System.Text.Encodings.Web;
using System.Text.Json;
using DeskInk.PcTool.Models;

namespace DeskInk.PcTool.Services;

/// <summary>把课表状态组装成 protocol.md 里定义的 JSON Lines 消息。</summary>
public static class ProtocolBuilder
{
    private static readonly JsonSerializerOptions Options = new()
    {
        Encoder = JavaScriptEncoder.UnsafeRelaxedJsonEscaping,
        DefaultIgnoreCondition = System.Text.Json.Serialization.JsonIgnoreCondition.WhenWritingNull
    };

    public static string BuildSchedule(LessonSnapshot snapshot)
    {
        var message = new ScheduleMessage
        {
            State = snapshot.StateCode,
            Cur = new ScheduleCurrent
            {
                Name = snapshot.CurrentName,
                Initial = snapshot.CurrentInitial,
                Start = snapshot.CurrentStart,
                End = snapshot.CurrentEnd,
                Left = snapshot.LeftMinutes
            },
            Next = string.IsNullOrEmpty(snapshot.NextName) && string.IsNullOrEmpty(snapshot.NextStart)
                ? null
                : new ScheduleNext
                {
                    Name = snapshot.NextName,
                    Start = snapshot.NextStart,
                    End = snapshot.NextEnd
                }
        };

        return JsonSerializer.Serialize(message, Options);
    }

    public static string BuildQuote(string text)
        => JsonSerializer.Serialize(new QuoteMessage { Text = text }, Options);

    /// <summary>待办每条固定开销就有 30 字节，8 条必然超 255，所以按每片 2 条分片。</summary>
    public const int TodoItemsPerChunk = 2;

    /// <summary>把待办拆成多帧（<c>seq</c>/<c>total</c>），每帧 2 条。</summary>
    public static IReadOnlyList<string> BuildTodoChunks(IEnumerable<TodoItem> items)
    {
        var list = items.Take(8).ToList();
        var chunks = new List<string>();

        for (var offset = 0; offset < list.Count; offset += TodoItemsPerChunk)
        {
            var slice = list.Skip(offset).Take(TodoItemsPerChunk).ToList();
            chunks.Add(JsonSerializer.Serialize(
                new TodoMessage
                {
                    Seq = offset / TodoItemsPerChunk,
                    Total = (list.Count + TodoItemsPerChunk - 1) / TodoItemsPerChunk,
                    Items = slice
                },
                Options));
        }

        return chunks;
    }

    /// <summary>设备小窗正文区 7 字 × 7 行 = 49 个汉字。</summary>
    public const int NewsBodyMaxChars = 49;

    /// <summary>新闻：title 留空，正文放 body（设备端 body 自动换行）。</summary>
    public static string BuildNews(string body, string title = "")
        => JsonSerializer.Serialize(new NewsMessage { Title = title, Body = TruncateNewsBody(body) }, Options);

    /// <summary>超出 49 个汉字就截断补「…」（不要发过去让设备自己切）。</summary>
    public static string TruncateNewsBody(string text)
        => text.Length <= NewsBodyMaxChars ? text : text[..NewsBodyMaxChars] + "…";

    public static string BuildImgBegin(int id, string name, int bytes, int total)
        => JsonSerializer.Serialize(new ImgBeginMessage { Id = id, Name = name, Bytes = bytes, Total = total }, Options);

    public static string BuildImgChunk(int id, int seq, string base64)
        => JsonSerializer.Serialize(new ImgChunkMessage { Id = id, Seq = seq, Data = base64 }, Options);

    public static string BuildImgEnd(int id, int crc)
        => JsonSerializer.Serialize(new ImgEndMessage { Id = id, Crc = crc }, Options);
}

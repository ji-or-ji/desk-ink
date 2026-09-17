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

    public static string BuildTodo(IEnumerable<TodoItem> items)
        => JsonSerializer.Serialize(new TodoMessage { Items = items.Take(8).ToList() }, Options);
}

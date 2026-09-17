using System.Collections.ObjectModel;
using DeskInk.PcTool.Models;

namespace DeskInk.PcTool.Services;

public sealed class LogService
{
    public ObservableCollection<LogEntry> Entries { get; } = new();

    public int MaxEntries { get; set; } = 200;

    public void Add(LogLevel level, string message)
    {
        var entry = new LogEntry(DateTime.Now, level, message);
        Ui.Post(() =>
        {
            Entries.Insert(0, entry);
            while (Entries.Count > MaxEntries)
            {
                Entries.RemoveAt(Entries.Count - 1);
            }
        });
    }

    public void Info(string message) => Add(LogLevel.Info, message);

    public void Push(string message) => Add(LogLevel.Push, message);

    public void Warn(string message) => Add(LogLevel.Warn, message);

    public void Error(string message) => Add(LogLevel.Error, message);
}

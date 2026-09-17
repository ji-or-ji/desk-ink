using System.Collections.ObjectModel;
using DeskInk.PcTool.Models;

namespace DeskInk.PcTool.Services;

public sealed class LogService
{
    private const long MaxLogFileBytes = 2 * 1024 * 1024;

    private readonly object _fileLock = new();
    private readonly string _logFile;

    public LogService()
    {
        var dir = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "DeskInk");
        Directory.CreateDirectory(dir);
        _logFile = Path.Combine(dir, "pc-tool.log");
    }

    public ObservableCollection<LogEntry> Entries { get; } = new();

    public int MaxEntries { get; set; } = 200;

    /// <summary>日志文件路径（%LOCALAPPDATA%\DeskInk\pc-tool.log）。</summary>
    public string LogFilePath => _logFile;

    public void Add(LogLevel level, string message)
    {
        var entry = new LogEntry(DateTime.Now, level, message);
        WriteToFile(entry);

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

    private void WriteToFile(LogEntry entry)
    {
        try
        {
            lock (_fileLock)
            {
                var info = new FileInfo(_logFile);
                if (info.Exists && info.Length > MaxLogFileBytes)
                {
                    File.Delete(_logFile);
                }

                File.AppendAllText(
                    _logFile,
                    $"{entry.Time:yyyy-MM-dd HH:mm:ss} [{entry.LevelText}] {entry.Message}{Environment.NewLine}");
            }
        }
        catch
        {
            // 日志写盘失败不影响主功能。
        }
    }
}

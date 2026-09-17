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

    public string Quote { get; set; } = string.Empty;

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

using DeskInk.PcTool.Models;

namespace DeskInk.PcTool.Services;

/// <summary>
/// 设备配置的内存镜像。只读：PC 只接收设备上报的 <c>cfg</c>，从不写回。
/// 文案必须与设备端 <c>config_value_label()</c> 一字不差。
/// </summary>
public sealed class DeviceConfigService
{
    public DeviceConfig? Current { get; private set; }

    public DateTime? LastSync { get; private set; }

    public event Action? Changed;

    public void Update(DeviceConfig config)
    {
        Current = config;
        LastSync = DateTime.Now;
        Changed?.Invoke();
    }

    public void Clear()
    {
        if (Current is null && LastSync is null)
        {
            return;
        }

        Current = null;
        LastSync = null;
        Changed?.Invoke();
    }

    public static string RefreshText(int value) => value switch
    {
        0 => "自动",
        1 => "常规",
        2 => "静默",
        _ => "—"
    };

    public static string MinutesText(int value) => value > 0 ? $"{value} 分" : "—";

    public static string RotateText(int value) => value switch
    {
        0 => "关",
        1 => "开",
        _ => "—"
    };

    public static string ArtModeText(int value) => value switch
    {
        0 => "全屏",
        1 => "小窗",
        _ => "—"
    };

    public static string BarModeText(int value) => value switch
    {
        0 => "实心",
        1 => "反色",
        2 => "反色自适应",
        _ => "—"
    };
}

namespace DeskInk.PcTool.Models;

/// <summary>设备上报的配置快照（PC 只镜像，不写回）。</summary>
public sealed record DeviceConfig(
    int Refresh,
    int PomoFocus,
    int PomoBreak,
    int Rotate,
    int ArtMode,
    int BarMode);

/// <summary>预览合成模式。</summary>
public enum ArtworkPreviewMode
{
    /// <summary>仅插画（现在的行为）。</summary>
    ImageOnly,

    /// <summary>全屏 + 实心信息条。</summary>
    FullSolid,

    /// <summary>全屏 + 反色信息条。</summary>
    FullInvert,

    /// <summary>全屏 + 反色自适应信息条。</summary>
    FullAuto,

    /// <summary>小窗模式：插画画在右列小窗位置。</summary>
    SmallWindow
}

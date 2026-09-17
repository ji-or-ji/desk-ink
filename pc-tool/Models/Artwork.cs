using System.ComponentModel;
using System.Drawing;
using Microsoft.UI.Xaml.Media;
using Windows.Foundation;

namespace DeskInk.PcTool.Models;

/// <summary>插画目标尺寸（对应设备配置项 cfg_art_mode）。</summary>
public enum ArtworkModeKind
{
    /// <summary>全屏 400×300。</summary>
    FullScreen,

    /// <summary>小窗 134×200。</summary>
    SmallWindow
}

/// <summary>缩放方式。</summary>
public enum ArtworkFit
{
    /// <summary>拉伸（会变形）。</summary>
    Stretch,

    /// <summary>居中裁剪后缩放（不变形）。</summary>
    Cover,

    /// <summary>等比缩放 + 留白。</summary>
    Contain
}

/// <summary>
/// 处理参数。<see cref="CropRect"/> 非空时用它作为源矩形（手动裁剪，忽略 <see cref="Fit"/>）。
/// </summary>
public sealed record ArtworkOptions(
    int TargetWidth,
    int TargetHeight,
    ArtworkFit Fit,
    bool UseDither,
    int Threshold,
    bool Invert,
    Rectangle? CropRect = null);

/// <summary>列表里的一张插画（原图 + 处理后的 1-bit 数据）。</summary>
public sealed class ArtworkEntry : INotifyPropertyChanged
{
    private int _width;
    private int _height;
    private byte[] _mono = Array.Empty<byte>();
    private ImageSource? _thumbnail;
    private bool _uploaded;

    public int Id { get; init; }

    public string Name { get; init; } = string.Empty;

    /// <summary>原图，改参数时重新处理用。</summary>
    public Bitmap? Source { get; init; }

    public int Width
    {
        get => _width;
        set
        {
            if (_width == value)
            {
                return;
            }

            _width = value;
            Raise(nameof(Width));
            Raise(nameof(SizeText));
        }
    }

    public int Height
    {
        get => _height;
        set
        {
            if (_height == value)
            {
                return;
            }

            _height = value;
            Raise(nameof(Height));
            Raise(nameof(SizeText));
        }
    }

    /// <summary>处理后的 1-bit 位图，上传时用。</summary>
    public byte[] Mono
    {
        get => _mono;
        set
        {
            _mono = value;
            Raise(nameof(Mono));
            Raise(nameof(SizeText));
        }
    }

    /// <summary>手动裁剪框（相对原图的 0..1 归一化坐标），随图保存。</summary>
    public Rect? CropNormalized { get; set; }

    /// <summary>裁剪编辑器里显示的原图（可能已降采样，比例与原图一致）。</summary>
    public ImageSource? SourceImage { get; set; }

    /// <summary>列表缩略图。</summary>
    public ImageSource? Thumbnail
    {
        get => _thumbnail;
        set
        {
            _thumbnail = value;
            Raise(nameof(Thumbnail));
        }
    }

    public bool Uploaded
    {
        get => _uploaded;
        set
        {
            if (_uploaded == value)
            {
                return;
            }

            _uploaded = value;
            Raise(nameof(Uploaded));
            Raise(nameof(StatusText));
        }
    }

    public string SizeText => $"{Width}×{Height} · {Mono.Length} 字节";

    public string StatusText => Uploaded ? $"#{Id} 已上传" : $"#{Id} 未上传";

    public event PropertyChangedEventHandler? PropertyChanged;

    private void Raise(string name) => PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));
}

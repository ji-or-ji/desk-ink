using System.Drawing.Imaging;
using System.Runtime.InteropServices.WindowsRuntime;
using DeskInk.PcTool.Models;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Media.Imaging;

namespace DeskInk.PcTool.Services;

/// <summary>
/// 插画：id 分配 + 分片上传；图像处理在 <see cref="MonoBitmap"/>，预览渲染在 <see cref="Render"/>。
/// 规格见 docs/desk-eink-info/变更-插画-PC端.md。
/// </summary>
public sealed class ArtworkService
{
    /// <summary>每片原始字节数（base64 后 172 字符，整帧 &lt; 255）。</summary>
    private const int ChunkBytes = 128;

    /// <summary>片间间隔，避免灌爆蓝牙 SMB 缓冲。</summary>
    private static readonly TimeSpan ChunkDelay = TimeSpan.FromMilliseconds(20);

    private readonly SerialLinkService _serial;
    private readonly LogService _log;
    private readonly string _idFile;
    private readonly SemaphoreSlim _uploadLock = new(1, 1);

    private int _nextId = 1;

    public ArtworkService(SerialLinkService serial, LogService log)
    {
        _serial = serial;
        _log = log;

        var dir = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "DeskInk");
        Directory.CreateDirectory(dir);
        _idFile = Path.Combine(dir, "artwork-id.txt");
        LoadNextId();
    }

    /// <summary>(已发片数, 总片数)。</summary>
    public event Action<int, int>? UploadProgress;

    public bool IsUploading { get; private set; }

    public static (int Width, int Height) GetSize(ArtworkModeKind mode)
        => mode == ArtworkModeKind.SmallWindow ? (134, 200) : (400, 300);

    /// <summary>分配一个自增 id（本地记 id → 名称；设备端不回报列表，属已知限制）。</summary>
    public int AllocateId()
    {
        var id = _nextId++;
        TrySaveNextId();
        return id;
    }

    /// <summary>把 <see cref="System.Drawing.Bitmap"/> 编码成可显示的图像源（裁剪编辑器用）。</summary>
    public static async Task<ImageSource> LoadImageSourceAsync(System.Drawing.Bitmap bitmap)
    {
        using var stream = new MemoryStream();
        bitmap.Save(stream, ImageFormat.Png);
        stream.Position = 0;

        var image = new BitmapImage();
        await image.SetSourceAsync(stream.AsRandomAccessStream());
        return image;
    }

    /// <summary>把 1-bit 数据渲染成位图，供预览/缩略图显示（scale 为放大倍数）。</summary>
    public static WriteableBitmap Render(byte[] mono, int width, int height, int scale)
    {
        scale = Math.Max(1, scale);
        var stride = (width + 7) / 8;
        var bitmap = new WriteableBitmap(width * scale, height * scale);
        var buffer = new byte[width * scale * height * scale * 4];
        var rowBytes = width * scale * 4;

        for (var y = 0; y < height; y++)
        {
            for (var x = 0; x < width; x++)
            {
                var black = (mono[y * stride + (x >> 3)] & (0x80 >> (x & 7))) != 0;
                var value = black ? (byte)0 : (byte)255;

                for (var dy = 0; dy < scale; dy++)
                {
                    var rowOffset = (y * scale + dy) * rowBytes + x * scale * 4;
                    for (var dx = 0; dx < scale; dx++)
                    {
                        var offset = rowOffset + dx * 4;
                        buffer[offset] = value;
                        buffer[offset + 1] = value;
                        buffer[offset + 2] = value;
                        buffer[offset + 3] = 255;
                    }
                }
            }
        }

        using (var stream = bitmap.PixelBuffer.AsStream())
        {
            stream.Write(buffer, 0, buffer.Length);
        }

        bitmap.Invalidate();
        return bitmap;
    }

    public async Task UploadAsync(byte[] mono, int id, string name, CancellationToken cancellationToken)
    {
        await _uploadLock.WaitAsync(cancellationToken).ConfigureAwait(false);
        IsUploading = true;
        try
        {
            var total = (mono.Length + ChunkBytes - 1) / ChunkBytes;
            var crc = MonoBitmap.Crc16Ccitt(mono);

            _log.Info($"开始上传插画 #{id}「{name}」：{mono.Length} 字节 / {total} 片，CRC=0x{crc:X4}");
            Send(ProtocolBuilder.BuildImgBegin(id, name, mono.Length, total));

            for (var seq = 0; seq < total; seq++)
            {
                cancellationToken.ThrowIfCancellationRequested();

                var offset = seq * ChunkBytes;
                var length = Math.Min(ChunkBytes, mono.Length - offset);
                var base64 = Convert.ToBase64String(mono, offset, length);

                Send(ProtocolBuilder.BuildImgChunk(id, seq, base64));
                UploadProgress?.Invoke(seq + 1, total);

                await Task.Delay(ChunkDelay, cancellationToken).ConfigureAwait(false);
            }

            Send(ProtocolBuilder.BuildImgEnd(id, crc));
            _log.Info($"插画 #{id} 上传完成（{total} 片）");
        }
        finally
        {
            IsUploading = false;
            _uploadLock.Release();
        }
    }

    private void Send(string line)
    {
        if (!_serial.SendLine(line))
        {
            throw new InvalidOperationException("串口未打开，插画上传中断");
        }
    }

    private void LoadNextId()
    {
        try
        {
            if (File.Exists(_idFile) && int.TryParse(File.ReadAllText(_idFile), out var value) && value > 0)
            {
                _nextId = value;
            }
        }
        catch
        {
            _nextId = 1;
        }
    }

    private void TrySaveNextId()
    {
        try
        {
            File.WriteAllText(_idFile, _nextId.ToString());
        }
        catch
        {
            // id 写失败不致命。
        }
    }
}

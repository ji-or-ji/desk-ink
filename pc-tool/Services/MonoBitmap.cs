using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
using DeskInk.PcTool.Models;

namespace DeskInk.PcTool.Services;

/// <summary>
/// 纯图像处理（不依赖 WinUI，方便单独测试）：缩放 / 二值化 / 抖动 / 反色 → 1-bit 打包位图。
/// 规格见 docs/desk-eink-info/变更-插画-PC端.md 第四节。
/// </summary>
public static class MonoBitmap
{
    /// <summary>4×4 有序抖动矩阵（Bayer），取值 0..15。</summary>
    private static readonly int[,] Bayer4 =
    {
        { 0, 8, 2, 10 },
        { 12, 4, 14, 6 },
        { 3, 11, 1, 9 },
        { 15, 7, 13, 5 }
    };

    /// <summary>
    /// 行优先、每行从左到右；位序 MSB 在左；1 = 黑点（涂墨）；stride = ceil(width / 8)。
    /// </summary>
    public static byte[] ToMonoBitmap(Bitmap source, ArtworkOptions options)
    {
        var width = options.TargetWidth;
        var height = options.TargetHeight;
        var stride = (width + 7) / 8;
        var result = new byte[stride * height];

        using var canvas = new Bitmap(width, height, PixelFormat.Format32bppArgb);
        using (var graphics = Graphics.FromImage(canvas))
        {
            graphics.Clear(Color.White);
            graphics.InterpolationMode = InterpolationMode.HighQualityBicubic;
            graphics.PixelOffsetMode = PixelOffsetMode.HighQuality;
            graphics.SmoothingMode = SmoothingMode.HighQuality;

            var (sourceRect, destRect) = options.CropRect is { Width: > 0, Height: > 0 } crop
                ? (crop, new Rectangle(0, 0, width, height))
                : ComputeLayout(source.Width, source.Height, width, height, options.Fit);
            graphics.DrawImage(source, destRect, sourceRect, GraphicsUnit.Pixel);
        }

        var data = canvas.LockBits(
            new Rectangle(0, 0, width, height),
            ImageLockMode.ReadOnly,
            PixelFormat.Format32bppArgb);

        try
        {
            var pixels = new byte[data.Stride * height];
            Marshal.Copy(data.Scan0, pixels, 0, pixels.Length);

            for (var y = 0; y < height; y++)
            {
                var rowOffset = y * data.Stride;
                var outOffset = y * stride;
                for (var x = 0; x < width; x++)
                {
                    var offset = rowOffset + x * 4;
                    var gray = (pixels[offset + 2] * 299 + pixels[offset + 1] * 587 + pixels[offset] * 114) / 1000;

                    var black = options.UseDither
                        ? gray < (Bayer4[y % 4, x % 4] + 0.5) / 16.0 * 255.0
                        : gray < options.Threshold;

                    if (options.Invert)
                    {
                        black = !black;
                    }

                    if (black)
                    {
                        result[outOffset + (x >> 3)] |= (byte)(0x80 >> (x & 7));
                    }
                }
            }
        }
        finally
        {
            canvas.UnlockBits(data);
        }

        return result;
    }

    /// <summary>把 1-bit 数据还原成位图（1 = 黑），用于导出 PNG。</summary>
    public static Bitmap ToBitmap(byte[] mono, int width, int height)
    {
        var stride = (width + 7) / 8;
        var bitmap = new Bitmap(width, height, PixelFormat.Format32bppArgb);
        var data = bitmap.LockBits(
            new Rectangle(0, 0, width, height),
            ImageLockMode.WriteOnly,
            PixelFormat.Format32bppArgb);

        try
        {
            var pixels = new byte[data.Stride * height];
            for (var y = 0; y < height; y++)
            {
                for (var x = 0; x < width; x++)
                {
                    var black = (mono[y * stride + (x >> 3)] & (0x80 >> (x & 7))) != 0;
                    var value = black ? (byte)0 : (byte)255;
                    var offset = y * data.Stride + x * 4;
                    pixels[offset] = value;
                    pixels[offset + 1] = value;
                    pixels[offset + 2] = value;
                    pixels[offset + 3] = 255;
                }
            }

            Marshal.Copy(pixels, 0, data.Scan0, pixels.Length);
        }
        finally
        {
            bitmap.UnlockBits(data);
        }

        return bitmap;
    }

    /// <summary>给裁剪编辑器用的显示副本：最长边不超过 maxSize（比例不变）。</summary>
    public static Bitmap CreateDisplayCopy(Bitmap source, int maxSize)
    {
        var scale = Math.Min(1.0, (double)maxSize / Math.Max(source.Width, source.Height));
        if (scale >= 1.0)
        {
            return new Bitmap(source);
        }

        var width = Math.Max(1, (int)Math.Round(source.Width * scale));
        var height = Math.Max(1, (int)Math.Round(source.Height * scale));
        var result = new Bitmap(width, height);
        using var graphics = Graphics.FromImage(result);
        graphics.InterpolationMode = InterpolationMode.HighQualityBicubic;
        graphics.PixelOffsetMode = PixelOffsetMode.HighQuality;
        graphics.DrawImage(source, 0, 0, width, height);
        return result;
    }

    /// <summary>CRC16-CCITT：多项式 0x1021，初值 0xFFFF，不反转。</summary>
    public static int Crc16Ccitt(byte[] data)
    {
        ushort crc = 0xFFFF;
        foreach (var value in data)
        {
            crc ^= (ushort)(value << 8);
            for (var bit = 0; bit < 8; bit++)
            {
                crc = (crc & 0x8000) != 0 ? (ushort)((crc << 1) ^ 0x1021) : (ushort)(crc << 1);
            }
        }

        return crc;
    }

    private static (Rectangle Source, Rectangle Dest) ComputeLayout(
        int sourceWidth,
        int sourceHeight,
        int targetWidth,
        int targetHeight,
        ArtworkFit fit)
    {
        switch (fit)
        {
            case ArtworkFit.Stretch:
                return (new Rectangle(0, 0, sourceWidth, sourceHeight), new Rectangle(0, 0, targetWidth, targetHeight));

            case ArtworkFit.Cover:
            {
                var scale = Math.Max((double)targetWidth / sourceWidth, (double)targetHeight / sourceHeight);
                var cropWidth = Math.Min(sourceWidth, Math.Max(1, (int)Math.Round(targetWidth / scale)));
                var cropHeight = Math.Min(sourceHeight, Math.Max(1, (int)Math.Round(targetHeight / scale)));
                var x = (sourceWidth - cropWidth) / 2;
                var y = (sourceHeight - cropHeight) / 2;
                return (new Rectangle(x, y, cropWidth, cropHeight), new Rectangle(0, 0, targetWidth, targetHeight));
            }

            default:
            {
                var scale = Math.Min((double)targetWidth / sourceWidth, (double)targetHeight / sourceHeight);
                var width = Math.Max(1, (int)Math.Round(sourceWidth * scale));
                var height = Math.Max(1, (int)Math.Round(sourceHeight * scale));
                var x = (targetWidth - width) / 2;
                var y = (targetHeight - height) / 2;
                return (new Rectangle(0, 0, sourceWidth, sourceHeight), new Rectangle(x, y, width, height));
            }
        }
    }
}

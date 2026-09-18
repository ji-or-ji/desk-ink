namespace DeskInk.PcTool.Services;

/// <summary>
/// 1-bit 画布：行优先、MSB 在左、1 = 黑。对应固件的 framebuffer 与
/// <c>tools/render-preview.ps1</c> 里的 <c>New-Fb</c> / <c>Set-Px</c> 一族。
/// </summary>
public sealed class MonoCanvas
{
    public MonoCanvas(int width, int height)
    {
        Width = width;
        Height = height;
        Stride = (width + 7) / 8;
        Buffer = new byte[Stride * height];
    }

    public int Width { get; }

    public int Height { get; }

    public int Stride { get; }

    public byte[] Buffer { get; }

    public void SetPixel(int x, int y, bool black)
    {
        if (x < 0 || x >= Width || y < 0 || y >= Height)
        {
            return;
        }

        var index = y * Stride + (x >> 3);
        var mask = (byte)(0x80 >> (x & 7));
        if (black)
        {
            Buffer[index] |= mask;
        }
        else
        {
            Buffer[index] &= (byte)~mask;
        }
    }

    public bool GetPixel(int x, int y)
    {
        if (x < 0 || x >= Width || y < 0 || y >= Height)
        {
            return false;
        }

        return (Buffer[y * Stride + (x >> 3)] & (0x80 >> (x & 7))) != 0;
    }

    public void FillRect(int x, int y, int width, int height, bool black)
    {
        for (var yy = Math.Max(0, y); yy < Math.Min(Height, y + height); yy++)
        {
            for (var xx = Math.Max(0, x); xx < Math.Min(Width, x + width); xx++)
            {
                SetPixel(xx, yy, black);
            }
        }
    }

    public void InvertRect(int x, int y, int width, int height)
    {
        for (var yy = Math.Max(0, y); yy < Math.Min(Height, y + height); yy++)
        {
            for (var xx = Math.Max(0, x); xx < Math.Min(Width, x + width); xx++)
            {
                var index = yy * Stride + (xx >> 3);
                Buffer[index] ^= (byte)(0x80 >> (xx & 7));
            }
        }
    }

    public int CountDark(int x, int y, int width, int height)
    {
        var count = 0;
        for (var yy = Math.Max(0, y); yy < Math.Min(Height, y + height); yy++)
        {
            for (var xx = Math.Max(0, x); xx < Math.Min(Width, x + width); xx++)
            {
                if (GetPixel(xx, yy))
                {
                    count++;
                }
            }
        }

        return count;
    }

    /// <summary>把一张 1-bit 图（自带宽高）贴到 (dstX, dstY)。</summary>
    public void Blit(byte[] source, int sourceWidth, int sourceHeight, int dstX, int dstY)
    {
        var sourceStride = (sourceWidth + 7) / 8;
        for (var y = 0; y < sourceHeight; y++)
        {
            for (var x = 0; x < sourceWidth; x++)
            {
                if ((source[y * sourceStride + (x >> 3)] & (0x80 >> (x & 7))) != 0)
                {
                    SetPixel(dstX + x, dstY + y, true);
                }
            }
        }
    }
}

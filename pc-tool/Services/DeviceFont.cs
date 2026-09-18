namespace DeskInk.PcTool.Services;

/// <summary>
/// 设备端真实点阵字库。格式见 <c>software/font/README.md</c>：
/// <c>F16B</c> + uint16 N + uint16 codes[N] + 每字 32 字节（16 行 × 2 字节，MSB 在左）。
/// 用真字库预览，信息条长度才和屏上一致。
/// </summary>
public sealed class DeviceFont
{
    private const int HanGlyphBytes = 32;

    private readonly ushort[] _hanCodes;
    private readonly byte[] _hanData;
    private readonly int _hanBase;
    private readonly byte[] _asciiData;

    private DeviceFont(ushort[] hanCodes, byte[] hanData, int hanBase, byte[] asciiData)
    {
        _hanCodes = hanCodes;
        _hanData = hanData;
        _hanBase = hanBase;
        _asciiData = asciiData;
    }

    /// <summary>字库目录（随工具打包到输出目录的 Assets 下）。</summary>
    public static string FontDirectory => Path.Combine(AppContext.BaseDirectory, "Assets");

    public static DeviceFont? TryLoadDefault()
    {
        var hanPath = Path.Combine(FontDirectory, "font16_simsun.bin");
        var asciiPath = Path.Combine(FontDirectory, "ascii8x16.bin");
        return File.Exists(hanPath) && File.Exists(asciiPath) ? Load(hanPath, asciiPath) : null;
    }

    public static DeviceFont Load(string hanPath, string asciiPath)
    {
        var han = File.ReadAllBytes(hanPath);
        var ascii = File.ReadAllBytes(asciiPath);

        if (han.Length < 6 || han[0] != 'F' || han[1] != '1' || han[2] != '6' || han[3] != 'B')
        {
            throw new InvalidDataException($"字库 magic 不是 F16B：{hanPath}");
        }

        var count = BitConverter.ToUInt16(han, 4);
        var codes = new ushort[count];
        System.Buffer.BlockCopy(han, 6, codes, 0, count * 2);

        return new DeviceFont(codes, han, 6 + count * 2, ascii);
    }

    public int MeasureText(string text)
    {
        var width = 0;
        foreach (var ch in text)
        {
            width += ch < 128 ? 8 : 16;
        }

        return width;
    }

    public void DrawText(MonoCanvas canvas, int x, int y, string text, bool black)
    {
        var cursor = x;
        foreach (var ch in text)
        {
            var code = (int)ch;
            if (code < 128)
            {
                if (code is < 32 or > 126)
                {
                    cursor += 8;
                    continue;
                }

                var offset = (code - 32) * 16;
                for (var row = 0; row < 16; row++)
                {
                    var bits = _asciiData[offset + row];
                    for (var col = 0; col < 8; col++)
                    {
                        if ((bits & (0x80 >> col)) != 0)
                        {
                            canvas.SetPixel(cursor + col, y + row, black);
                        }
                    }
                }

                cursor += 8;
            }
            else
            {
                var index = Array.BinarySearch(_hanCodes, (ushort)code);
                if (index >= 0)
                {
                    var offset = _hanBase + index * HanGlyphBytes;
                    for (var row = 0; row < 16; row++)
                    {
                        var hi = _hanData[offset + row * 2];
                        var lo = _hanData[offset + row * 2 + 1];
                        for (var col = 0; col < 8; col++)
                        {
                            if ((hi & (0x80 >> col)) != 0)
                            {
                                canvas.SetPixel(cursor + col, y + row, black);
                            }

                            if ((lo & (0x80 >> col)) != 0)
                            {
                                canvas.SetPixel(cursor + 8 + col, y + row, black);
                            }
                        }
                    }
                }

                cursor += 16;
            }
        }
    }
}

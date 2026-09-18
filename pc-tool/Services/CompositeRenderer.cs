namespace DeskInk.PcTool.Services;

/// <summary>信息条模式，对应设备端 <c>bar_mode</c>。</summary>
public enum BarMode
{
    /// <summary>实心：条内填黑、画白字。</summary>
    Solid,

    /// <summary>反色：整条取反、画白字。</summary>
    Invert,

    /// <summary>反色自适应：逐段取反、按取反后明暗自动选字色。</summary>
    Auto
}

/// <summary>
/// 合成预览。算法与 <c>tools/render-preview.ps1</c> 一致（含布局常量与字色判定）。
/// </summary>
public static class CompositeRenderer
{
    public const int FullWidth = 400;
    public const int FullHeight = 300;
    public const int BarHeight = 32;

    // 小窗内容区（ui.c：PANEL_X=266, CONTENT_Y=100, 134×200）
    public const int PanelX = 266;
    public const int PanelY = 100;
    public const int PanelWidth = 134;
    public const int PanelHeight = 200;

    /// <summary>全屏 400×300 + 顶部信息条。</summary>
    public static byte[] ComposeFull(
        byte[] image400x300,
        BarMode barMode,
        DeviceFont? font,
        string left,
        string right,
        int barHeight = BarHeight)
    {
        var canvas = new MonoCanvas(FullWidth, FullHeight);
        canvas.Blit(image400x300, FullWidth, FullHeight, 0, 0);
        DrawBar(canvas, barMode, font, left, right, barHeight);
        return canvas.Buffer;
    }

    /// <summary>小窗：400×300 画布，插画贴到右列内容区 (266,100)。</summary>
    public static byte[] ComposeSmallWindow(byte[] image134x200)
    {
        var canvas = new MonoCanvas(FullWidth, FullHeight);
        canvas.Blit(image134x200, PanelWidth, PanelHeight, PanelX, PanelY);
        return canvas.Buffer;
    }

    private static void DrawBar(
        MonoCanvas canvas,
        BarMode mode,
        DeviceFont? font,
        string left,
        string right,
        int height)
    {
        height = Math.Min(height, canvas.Height);
        if (height <= 0)
        {
            return;
        }

        var textY = (height - 16) / 2;

        switch (mode)
        {
            case BarMode.Solid:
                canvas.FillRect(0, 0, canvas.Width, height, true);
                DrawPair(canvas, font, left, right, textY);
                break;

            case BarMode.Invert:
                canvas.InvertRect(0, 0, canvas.Width, height);
                DrawPair(canvas, font, left, right, textY);
                break;

            default:
                if (font is null)
                {
                    return;
                }

                DrawSegmentAuto(canvas, font, 10, textY, left);
                DrawSegmentAuto(canvas, font, canvas.Width - 12 - font.MeasureText(right), textY, right);
                break;
        }
    }

    private static void DrawPair(MonoCanvas canvas, DeviceFont? font, string left, string right, int textY)
    {
        if (font is null)
        {
            return;
        }

        font.DrawText(canvas, 12, textY, left, false);
        font.DrawText(canvas, canvas.Width - 12 - font.MeasureText(right), textY, right, false);
    }

    private static void DrawSegmentAuto(MonoCanvas canvas, DeviceFont font, int x, int y, string text)
    {
        var width = font.MeasureText(text);
        if (width <= 0)
        {
            return;
        }

        const int pad = 2;
        var rx = x - pad;
        var ry = y - pad;
        var rw = width + pad * 2;
        var rh = 16 + pad * 2;
        var area = rw * rh;

        var dark = canvas.CountDark(rx, ry, rw, rh);
        canvas.InvertRect(rx, ry, rw, rh);
        var darkAfter = area - dark;
        var whiteText = darkAfter * 2 > area;

        font.DrawText(canvas, x, y, text, !whiteText);
    }
}

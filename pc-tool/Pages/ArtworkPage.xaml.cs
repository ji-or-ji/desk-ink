using System.Collections.ObjectModel;
using System.Drawing;
using DeskInk.PcTool.Models;
using DeskInk.PcTool.Services;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Navigation;
using Windows.Foundation;
using Windows.Storage.Pickers;
using WinRT.Interop;
using Point = Windows.Foundation.Point;

namespace DeskInk.PcTool.Pages;

public sealed partial class ArtworkPage : Page
{
    private const double HandleHitSize = 18;
    private const double MinCropSize = 24;

    private readonly AppServices _services = App.Instance.Services;
    private readonly ObservableCollection<ArtworkEntry> _entries = new();

    private ArtworkEntry? _selected;
    private CancellationTokenSource? _uploadCts;
    private bool _initialized;

    private Rect _imageRect;
    private Rect _cropRect;
    private Rect? _cropNormalized;
    private DragMode _dragMode;
    private Point _dragStart;
    private Rect _dragStartRect;

    public ArtworkPage()
    {
        InitializeComponent();
        ArtworkList.ItemsSource = _entries;

        ModeCombo.SelectedIndex = _services.Settings.Current.ArtworkMode == ArtworkModeKind.SmallWindow ? 1 : 0;
        FitCombo.SelectedIndex = 0;
        DitherSwitch.IsOn = true;
        InvertSwitch.IsOn = false;
        ThresholdBox.Value = 128;

        _initialized = true;
        UpdateThresholdEnabled();
        UpdateEmptyHint();
        UpdateCropEnabled();

        _services.Artwork.UploadProgress += OnUploadProgress;
    }

    protected override void OnNavigatedFrom(NavigationEventArgs e)
    {
        base.OnNavigatedFrom(e);

        // 上传过程中离开页面就取消，避免继续往串口灌数据。
        _uploadCts?.Cancel();
    }

    private enum DragMode
    {
        None,
        Move,
        TopLeft,
        TopRight,
        BottomLeft,
        BottomRight
    }

    private ArtworkModeKind SelectedMode
        => ((ModeCombo.SelectedItem as ComboBoxItem)?.Tag as string) == "SmallWindow"
            ? ArtworkModeKind.SmallWindow
            : ArtworkModeKind.FullScreen;

    private bool IsManualCrop => FitCombo.SelectedIndex <= 0;

    private double TargetAspect
    {
        get
        {
            var (width, height) = ArtworkService.GetSize(SelectedMode);
            return (double)width / height;
        }
    }

    // ---------------------------------------------------------------- 参数

    private ArtworkOptions CurrentOptions()
    {
        var (width, height) = ArtworkService.GetSize(SelectedMode);
        var threshold = double.IsNaN(ThresholdBox.Value) ? 128 : (int)Math.Clamp(ThresholdBox.Value, 0, 255);

        if (IsManualCrop)
        {
            return new ArtworkOptions(width, height, ArtworkFit.Stretch, DitherSwitch.IsOn, threshold, InvertSwitch.IsOn, SourceCropRect());
        }

        var fit = FitCombo.SelectedIndex == 1 ? ArtworkFit.Stretch : ArtworkFit.Contain;
        return new ArtworkOptions(width, height, fit, DitherSwitch.IsOn, threshold, InvertSwitch.IsOn);
    }

    /// <summary>把画布上的裁剪框换算成原图像素矩形。</summary>
    private Rectangle? SourceCropRect()
    {
        var source = _selected?.Source;
        if (source is null || _imageRect.Width <= 0 || _cropRect.Width <= 0)
        {
            return null;
        }

        var scale = source.Width / _imageRect.Width;
        var x = Math.Clamp((int)Math.Round((_cropRect.X - _imageRect.X) * scale), 0, source.Width - 1);
        var y = Math.Clamp((int)Math.Round((_cropRect.Y - _imageRect.Y) * scale), 0, source.Height - 1);
        var width = Math.Clamp((int)Math.Round(_cropRect.Width * scale), 1, source.Width - x);
        var height = Math.Clamp((int)Math.Round(_cropRect.Height * scale), 1, source.Height - y);
        return new Rectangle(x, y, width, height);
    }

    // ---------------------------------------------------------------- 列表

    private async void Import_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            var picker = new FileOpenPicker();
            foreach (var extension in new[] { ".jpg", ".jpeg", ".png", ".bmp", ".gif", ".webp" })
            {
                picker.FileTypeFilter.Add(extension);
            }

            var window = App.Instance.MainWindow;
            if (window is not null)
            {
                InitializeWithWindow.Initialize(picker, WindowNative.GetWindowHandle(window));
            }

            var file = await picker.PickSingleFileAsync();
            if (file is null)
            {
                return;
            }

            Bitmap source;
            using (var stream = File.OpenRead(file.Path))
            using (var loaded = new Bitmap(stream))
            {
                // 复制一份，避免 Bitmap 一直占着原文件。
                source = new Bitmap(loaded);
            }

            var (width, height) = ArtworkService.GetSize(SelectedMode);
            var entry = new ArtworkEntry
            {
                Id = _services.Artwork.AllocateId(),
                Name = file.Name,
                Source = source,
                Width = width,
                Height = height
            };

            using (var display = MonoBitmap.CreateDisplayCopy(source, 1024))
            {
                entry.SourceImage = await ArtworkService.LoadImageSourceAsync(display);
            }

            _entries.Add(entry);
            ArtworkList.SelectedItem = entry;
            UpdateEmptyHint();

            _services.Log.Info($"导入插画「{entry.Name}」→ #{entry.Id}，原图 {source.Width}×{source.Height}");
        }
        catch (Exception ex)
        {
            _services.Log.Error($"导入插画失败：{ex.Message}");
        }
    }

    private void Remove_Click(object sender, RoutedEventArgs e)
    {
        if (_selected is null)
        {
            return;
        }

        var entry = _selected;
        _entries.Remove(entry);
        entry.Source?.Dispose();

        _selected = null;
        _cropNormalized = null;
        RefreshPreview();
        UpdateEmptyHint();
        UpdateCropEnabled();
        UploadStatusText.Text = string.Empty;
    }

    private void ArtworkList_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        _selected = ArtworkList.SelectedItem as ArtworkEntry;
        _cropNormalized = _selected?.CropNormalized;

        LayoutCropEditor();
        ReprocessSelected();
        UploadStatusText.Text = _selected?.StatusText ?? string.Empty;
        UploadProgressBar.Value = 0;
    }

    // ---------------------------------------------------------------- 参数变化

    private void ModeCombo_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (!_initialized)
        {
            return;
        }

        _services.Settings.Current.ArtworkMode = SelectedMode;
        _services.Settings.Save();

        // 目标比例变了，旧的裁剪框不适用，重置。
        _cropNormalized = null;
        if (_selected is not null)
        {
            _selected.CropNormalized = null;
        }

        LayoutCropEditor();
        ReprocessSelected();
    }

    private void FitCombo_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        UpdateCropEnabled();
        ReprocessSelected();
    }

    private void ThresholdBox_ValueChanged(NumberBox sender, NumberBoxValueChangedEventArgs args) => ReprocessSelected();

    private void DitherSwitch_Toggled(object sender, RoutedEventArgs e)
    {
        UpdateThresholdEnabled();
        ReprocessSelected();
    }

    private void InvertSwitch_Toggled(object sender, RoutedEventArgs e) => ReprocessSelected();

    private void UpdateThresholdEnabled() => ThresholdBox.IsEnabled = !DitherSwitch.IsOn;

    private void UpdateCropEnabled()
    {
        var manual = IsManualCrop;
        CropCanvas.Opacity = manual ? 1 : 0.35;
        CropCanvas.IsHitTestVisible = manual;
        CropHintText.Text = manual ? "裁剪（拖动方框选位置，拖角缩放）" : "裁剪（当前为自动缩放）";
    }

    private void ReprocessSelected(bool updateThumbnail = true)
    {
        if (!_initialized || _selected?.Source is null)
        {
            return;
        }

        var (width, height) = ArtworkService.GetSize(SelectedMode);
        _selected.Width = width;
        _selected.Height = height;
        _selected.Mono = MonoBitmap.ToMonoBitmap(_selected.Source, CurrentOptions());
        if (updateThumbnail)
        {
            _selected.Thumbnail = ArtworkService.Render(_selected.Mono, width, height, 1);
        }

        _selected.Uploaded = false;
        RefreshPreview();
    }

    private void PreviewHost_SizeChanged(object sender, SizeChangedEventArgs e) => RefreshPreview();

    private void RefreshPreview()
    {
        if (_selected is null)
        {
            PreviewImage.Source = null;
            return;
        }

        var (width, height) = (_selected.Width, _selected.Height);

        // 取能塞进预览区的最大的整数放大倍数，其余交给 Image 的 Uniform 自适应。
        var scale = 1;
        var hostWidth = PreviewHost.ActualWidth;
        var hostHeight = PreviewHost.ActualHeight;
        if (hostWidth > 0 && hostHeight > 0)
        {
            scale = Math.Max(1, (int)Math.Floor(Math.Min(hostWidth / width, hostHeight / height)));
        }

        PreviewImage.Source = ArtworkService.Render(_selected.Mono, width, height, scale);
        PreviewTitleText.Text = $"1-bit 预览 · {width}×{height} · {_selected.Mono.Length} 字节";
    }

    // ---------------------------------------------------------------- 裁剪编辑器

    private void CropCanvas_SizeChanged(object sender, SizeChangedEventArgs e) => LayoutCropEditor();

    private void LayoutCropEditor()
    {
        var source = _selected?.Source;
        var canvasWidth = CropCanvas.ActualWidth;
        var canvasHeight = CropCanvas.ActualHeight;
        if (source is null || canvasWidth <= 0 || canvasHeight <= 0)
        {
            CropImage.Source = null;
            return;
        }

        CropImage.Source = _selected!.SourceImage;

        var scale = Math.Min(canvasWidth / source.Width, canvasHeight / source.Height);
        var width = source.Width * scale;
        var height = source.Height * scale;
        _imageRect = new Rect((canvasWidth - width) / 2, (canvasHeight - height) / 2, width, height);

        Canvas.SetLeft(CropImage, _imageRect.X);
        Canvas.SetTop(CropImage, _imageRect.Y);
        CropImage.Width = _imageRect.Width;
        CropImage.Height = _imageRect.Height;

        if (_cropNormalized is { Width: > 0, Height: > 0 } normalized)
        {
            _cropRect = new Rect(
                _imageRect.X + normalized.X * _imageRect.Width,
                _imageRect.Y + normalized.Y * _imageRect.Height,
                normalized.Width * _imageRect.Width,
                normalized.Height * _imageRect.Height);
        }
        else
        {
            ResetCropRect();
        }

        UpdateCropVisuals();
    }

    private void ResetCropRect()
    {
        var width = _imageRect.Width;
        var height = width / TargetAspect;
        if (height > _imageRect.Height)
        {
            height = _imageRect.Height;
            width = height * TargetAspect;
        }

        _cropRect = new Rect(
            _imageRect.X + (_imageRect.Width - width) / 2,
            _imageRect.Y + (_imageRect.Height - height) / 2,
            width,
            height);
        SaveCropNormalized();
    }

    private void SaveCropNormalized()
    {
        if (_imageRect.Width <= 0 || _imageRect.Height <= 0 || _cropRect.Width <= 0)
        {
            return;
        }

        _cropNormalized = new Rect(
            (_cropRect.X - _imageRect.X) / _imageRect.Width,
            (_cropRect.Y - _imageRect.Y) / _imageRect.Height,
            _cropRect.Width / _imageRect.Width,
            _cropRect.Height / _imageRect.Height);

        if (_selected is not null)
        {
            _selected.CropNormalized = _cropNormalized;
        }
    }

    private void UpdateCropVisuals()
    {
        var geometry = new GeometryGroup { FillRule = FillRule.EvenOdd };
        geometry.Children.Add(new RectangleGeometry { Rect = _imageRect });
        geometry.Children.Add(new RectangleGeometry { Rect = _cropRect });
        CropShade.Data = geometry;

        Canvas.SetLeft(CropRectVisual, _cropRect.X);
        Canvas.SetTop(CropRectVisual, _cropRect.Y);
        CropRectVisual.Width = _cropRect.Width;
        CropRectVisual.Height = _cropRect.Height;

        PositionHandle(HandleTopLeft, _cropRect.X, _cropRect.Y);
        PositionHandle(HandleTopRight, _cropRect.Right, _cropRect.Y);
        PositionHandle(HandleBottomLeft, _cropRect.X, _cropRect.Bottom);
        PositionHandle(HandleBottomRight, _cropRect.Right, _cropRect.Bottom);
    }

    private static void PositionHandle(FrameworkElement handle, double x, double y)
    {
        Canvas.SetLeft(handle, x - handle.Width / 2);
        Canvas.SetTop(handle, y - handle.Height / 2);
    }

    private void CropCanvas_PointerPressed(object sender, PointerRoutedEventArgs e)
    {
        if (!IsManualCrop || _selected?.Source is null)
        {
            return;
        }

        var point = e.GetCurrentPoint(CropCanvas).Position;
        var corner = HitCorner(point);
        _dragMode = corner != DragMode.None
            ? corner
            : (Contains(_cropRect, point) ? DragMode.Move : DragMode.None);

        if (_dragMode == DragMode.None)
        {
            return;
        }

        _dragStart = point;
        _dragStartRect = _cropRect;
        CropCanvas.CapturePointer(e.Pointer);
        e.Handled = true;
    }

    private void CropCanvas_PointerMoved(object sender, PointerRoutedEventArgs e)
    {
        if (_dragMode == DragMode.None)
        {
            return;
        }

        var point = e.GetCurrentPoint(CropCanvas).Position;
        if (_dragMode == DragMode.Move)
        {
            var x = Math.Clamp(
                _dragStartRect.X + (point.X - _dragStart.X),
                _imageRect.X,
                _imageRect.Right - _dragStartRect.Width);
            var y = Math.Clamp(
                _dragStartRect.Y + (point.Y - _dragStart.Y),
                _imageRect.Y,
                _imageRect.Bottom - _dragStartRect.Height);
            _cropRect = new Rect(x, y, _dragStartRect.Width, _dragStartRect.Height);
        }
        else
        {
            _cropRect = ResizeCrop(_dragMode, point);
        }

        SaveCropNormalized();
        UpdateCropVisuals();
        ReprocessSelected(updateThumbnail: false);
        e.Handled = true;
    }

    private void CropCanvas_PointerReleased(object sender, PointerRoutedEventArgs e) => EndDrag();

    private void CropCanvas_PointerCanceled(object sender, PointerRoutedEventArgs e) => EndDrag();

    private void CropCanvas_PointerCaptureLost(object sender, PointerRoutedEventArgs e) => EndDrag();

    private void EndDrag()
    {
        if (_dragMode == DragMode.None)
        {
            return;
        }

        _dragMode = DragMode.None;
        ReprocessSelected();
    }

    /// <summary>从被拖的角出发，保持目标比例算新的裁剪框（对角固定）。</summary>
    private Rect ResizeCrop(DragMode mode, Point pointer)
    {
        var left = mode is DragMode.TopLeft or DragMode.BottomLeft;
        var top = mode is DragMode.TopLeft or DragMode.TopRight;

        var anchorX = left ? _dragStartRect.Right : _dragStartRect.X;
        var anchorY = top ? _dragStartRect.Bottom : _dragStartRect.Y;

        var maxWidth = left ? anchorX - _imageRect.X : _imageRect.Right - anchorX;
        var maxHeight = top ? anchorY - _imageRect.Y : _imageRect.Bottom - anchorY;

        var width = Math.Max(Math.Abs(pointer.X - anchorX), Math.Abs(pointer.Y - anchorY) * TargetAspect);
        width = Math.Min(width, maxWidth);
        var height = width / TargetAspect;
        if (height > maxHeight)
        {
            height = maxHeight;
            width = height * TargetAspect;
        }

        width = Math.Max(width, Math.Min(MinCropSize, maxWidth));
        height = width / TargetAspect;

        return new Rect(left ? anchorX - width : anchorX, top ? anchorY - height : anchorY, width, height);
    }

    private DragMode HitCorner(Point point)
    {
        if (Near(point, _cropRect.X, _cropRect.Y))
        {
            return DragMode.TopLeft;
        }

        if (Near(point, _cropRect.Right, _cropRect.Y))
        {
            return DragMode.TopRight;
        }

        if (Near(point, _cropRect.X, _cropRect.Bottom))
        {
            return DragMode.BottomLeft;
        }

        if (Near(point, _cropRect.Right, _cropRect.Bottom))
        {
            return DragMode.BottomRight;
        }

        return DragMode.None;
    }

    private static bool Near(Point point, double x, double y)
        => Math.Abs(point.X - x) <= HandleHitSize / 2 && Math.Abs(point.Y - y) <= HandleHitSize / 2;

    private static bool Contains(Rect rect, Point point)
        => point.X >= rect.X && point.X <= rect.Right && point.Y >= rect.Y && point.Y <= rect.Bottom;

    // ---------------------------------------------------------------- 上传

    private async void Upload_Click(object sender, RoutedEventArgs e)
    {
        if (_selected is null)
        {
            UploadStatusText.Text = "先选一张插画";
            return;
        }

        if (!_services.Serial.IsOpen)
        {
            UploadStatusText.Text = "串口未打开";
            return;
        }

        var entry = _selected;
        _uploadCts = new CancellationTokenSource();
        SetUploading(true);
        UploadProgressBar.Value = 0;

        try
        {
            await _services.Artwork.UploadAsync(entry.Mono, entry.Id, entry.Name, _uploadCts.Token);
            entry.Uploaded = true;
            UploadStatusText.Text = "上传完成";
        }
        catch (OperationCanceledException)
        {
            UploadStatusText.Text = "已取消";
        }
        catch (Exception ex)
        {
            UploadStatusText.Text = "上传失败";
            _services.Log.Error($"插画上传失败：{ex.Message}");
        }
        finally
        {
            SetUploading(false);
            _uploadCts.Dispose();
            _uploadCts = null;
        }
    }

    private void Cancel_Click(object sender, RoutedEventArgs e) => _uploadCts?.Cancel();

    private async void Export_Click(object sender, RoutedEventArgs e)
    {
        if (_selected is null)
        {
            UploadStatusText.Text = "先选一张插画";
            return;
        }

        try
        {
            var entry = _selected;
            var picker = new FileSavePicker
            {
                SuggestedFileName = $"{Path.GetFileNameWithoutExtension(entry.Name)}_{entry.Width}x{entry.Height}"
            };
            picker.FileTypeChoices.Add("PNG 图片", new List<string> { ".png" });
            picker.FileTypeChoices.Add("1-bit 原始数据", new List<string> { ".bin" });

            var window = App.Instance.MainWindow;
            if (window is not null)
            {
                InitializeWithWindow.Initialize(picker, WindowNative.GetWindowHandle(window));
            }

            var file = await picker.PickSaveFileAsync();
            if (file is null)
            {
                return;
            }

            if (file.FileType.Equals(".bin", StringComparison.OrdinalIgnoreCase))
            {
                await File.WriteAllBytesAsync(file.Path, entry.Mono);
                UploadStatusText.Text = $"已导出 1-bit 数据（{entry.Mono.Length} 字节）";
            }
            else
            {
                using var bitmap = MonoBitmap.ToBitmap(entry.Mono, entry.Width, entry.Height);
                bitmap.Save(file.Path, System.Drawing.Imaging.ImageFormat.Png);
                UploadStatusText.Text = $"已导出 PNG（{entry.Width}×{entry.Height}）";
            }

            _services.Log.Info($"导出插画 #{entry.Id}：{file.Path}");
        }
        catch (Exception ex)
        {
            UploadStatusText.Text = "导出失败";
            _services.Log.Error($"导出插画失败：{ex.Message}");
        }
    }

    private void SetUploading(bool uploading)
    {
        // 上传期间不允许并发第二张（img_begin 会顶掉旧任务）。
        UploadButton.IsEnabled = !uploading;
        CancelButton.IsEnabled = uploading;
        ExportButton.IsEnabled = !uploading;
        ImportButton.IsEnabled = !uploading;
        RemoveButton.IsEnabled = !uploading;
        ArtworkList.IsEnabled = !uploading;
        CropCanvas.IsHitTestVisible = !uploading && IsManualCrop;
    }

    private void OnUploadProgress(int sent, int total) => Ui.Post(() =>
    {
        UploadProgressBar.Maximum = total;
        UploadProgressBar.Value = sent;
        UploadStatusText.Text = $"第 {sent} / {total} 片";
    });

    private void UpdateEmptyHint() => EmptyHintText.Visibility = _entries.Count == 0 ? Visibility.Visible : Visibility.Collapsed;
}

using DeskInk.PcTool.Models;
using DeskInk.PcTool.Services;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Windows.Storage.Pickers;
using WinRT.Interop;

namespace DeskInk.PcTool.Pages;

public sealed partial class QuotePage : Page
{
    private readonly AppServices _services = App.Instance.Services;

    public QuotePage()
    {
        InitializeComponent();
        LoadSettings();

        _services.Quote.Changed += OnQuoteChanged;
        UpdatePreview();
    }

    private void LoadSettings()
    {
        var settings = _services.Settings.Current;
        PushQuoteSwitch.IsOn = settings.PushQuote;
        QuoteBox.Text = settings.Quote;
        QuoteUrlBox.Text = settings.QuoteUrl;
        QuoteFileBox.Text = settings.QuoteFile;

        QuoteSourceCombo.SelectedIndex = settings.QuoteSource switch
        {
            QuoteSourceKind.Manual => 1,
            QuoteSourceKind.Url => 2,
            QuoteSourceKind.File => 3,
            _ => 0
        };

        UpdateSourceVisibility();
    }

    private QuoteSourceKind SelectedQuoteSource
        => ((QuoteSourceCombo.SelectedItem as ComboBoxItem)?.Tag as string) switch
        {
            "Manual" => QuoteSourceKind.Manual,
            "Url" => QuoteSourceKind.Url,
            "File" => QuoteSourceKind.File,
            _ => QuoteSourceKind.Hitokoto
        };

    private void QuoteSourceCombo_SelectionChanged(object sender, SelectionChangedEventArgs e)
        => UpdateSourceVisibility();

    private void UpdateSourceVisibility()
    {
        var source = SelectedQuoteSource;
        QuoteBox.Visibility = source == QuoteSourceKind.Manual ? Visibility.Visible : Visibility.Collapsed;
        QuoteUrlBox.Visibility = source == QuoteSourceKind.Url ? Visibility.Visible : Visibility.Collapsed;
        QuoteFileRow.Visibility = source == QuoteSourceKind.File ? Visibility.Visible : Visibility.Collapsed;
    }

    private async void BrowseQuoteFile_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            var picker = new FileOpenPicker();
            picker.FileTypeFilter.Add("*");

            var window = App.Instance.MainWindow;
            if (window is not null)
            {
                InitializeWithWindow.Initialize(picker, WindowNative.GetWindowHandle(window));
            }

            var file = await picker.PickSingleFileAsync();
            if (file is not null)
            {
                QuoteFileBox.Text = file.Path;
            }
        }
        catch (Exception ex)
        {
            _services.Log.Warn($"选择文件失败：{ex.Message}");
        }
    }

    private async void Save_Click(object sender, RoutedEventArgs e)
    {
        ApplyQuoteFields();
        _services.Settings.Save();

        await _services.Quote.RefreshAsync("保存设置");
        UpdatePreview();
        StatusText.Text = "已保存";
    }

    private async void Fetch_Click(object sender, RoutedEventArgs e)
    {
        ApplyQuoteFields();
        var ok = await _services.Quote.RefreshAsync("手动获取");
        UpdatePreview();
        StatusText.Text = ok ? "已获取，记得保存" : "获取失败，看日志";
    }

    private void ApplyQuoteFields()
    {
        var settings = _services.Settings.Current;
        settings.PushQuote = PushQuoteSwitch.IsOn;
        settings.QuoteSource = SelectedQuoteSource;
        settings.Quote = QuoteBox.Text;
        settings.QuoteUrl = QuoteUrlBox.Text.Trim();
        settings.QuoteFile = QuoteFileBox.Text.Trim();
    }

    private void OnQuoteChanged() => UpdatePreview();

    private void UpdatePreview()
    {
        Ui.Post(() =>
        {
            var quote = _services.Quote.Current;
            QuoteText.Text = string.IsNullOrWhiteSpace(quote) ? "尚未获取" : quote;
            SourceText.Text = $"来源：{_services.Quote.SourceText}";
        });
    }
}

using System.Collections.ObjectModel;
using DeskInk.PcTool.Models;
using DeskInk.PcTool.Services;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Windows.System;

namespace DeskInk.PcTool.Pages;

public sealed partial class TodoPage : Page
{
    private const int MaxItems = 8;
    private const int MaxChars = 14;

    private readonly AppServices _services = App.Instance.Services;
    private readonly ObservableCollection<TodoItem> _items = new();

    public TodoPage()
    {
        InitializeComponent();
        TodoList.ItemsSource = _items;

        LoadFromSettings();

        _services.Push.TodoUpdated += OnTodoUpdated;
    }

    private void LoadFromSettings()
    {
        _items.Clear();
        foreach (var item in _services.Settings.Current.Todos.Take(MaxItems))
        {
            _items.Add(item);
        }

        UpdateHint();
        RefreshPreview();
    }

    private void OnTodoUpdated() => Ui.Post(LoadFromSettings);

    private void UpdateHint() => HintText.Text = $"{_items.Count} / {MaxItems} 条";

    private void Add_Click(object sender, RoutedEventArgs e)
    {
        var text = NewTodoBox.Text.Trim();
        if (text.Length == 0)
        {
            StatusText.Text = "先输入内容";
            return;
        }

        if (_items.Count >= MaxItems)
        {
            StatusText.Text = $"最多 {MaxItems} 条，先删一条";
            return;
        }

        if (text.Length > MaxChars)
        {
            StatusText.Text = $"单条最多 {MaxChars} 个汉字，当前 {text.Length} 个";
            return;
        }

        var id = _items.Count == 0 ? 1 : _items.Max(item => item.Id) + 1;
        _items.Add(new TodoItem { Id = id, Done = false, Text = text });
        NewTodoBox.Text = string.Empty;

        PersistAndRefresh();
        StatusText.Text = "已添加";
    }

    private void NewTodoBox_KeyDown(object sender, KeyRoutedEventArgs e)
    {
        if (e.Key != VirtualKey.Enter)
        {
            return;
        }

        Add_Click(sender, e);
        e.Handled = true;
    }

    private void Delete_Click(object sender, RoutedEventArgs e)
    {
        if (sender is not FrameworkElement { DataContext: TodoItem item })
        {
            return;
        }

        _items.Remove(item);
        PersistAndRefresh();
        StatusText.Text = "已删除";
    }

    private void TodoCheck_Click(object sender, RoutedEventArgs e)
    {
        if (sender is not CheckBox { DataContext: TodoItem item } box)
        {
            return;
        }

        item.Done = box.IsChecked == true;
        PersistAndRefresh();
    }

    private void Push_Click(object sender, RoutedEventArgs e)
    {
        if (_items.Count == 0)
        {
            StatusText.Text = "没有待办可推送";
            return;
        }

        if (!_services.Serial.IsOpen)
        {
            StatusText.Text = "串口未打开";
            return;
        }

        _services.Push.PushTodo("手动推送");
        StatusText.Text = $"已推送 {_items.Count} 条";
    }

    private void PersistAndRefresh()
    {
        _services.Settings.Current.Todos = _items.ToList();
        _services.Settings.Save();

        UpdateHint();
        RefreshPreview();
    }

    private void PreviewHost_SizeChanged(object sender, SizeChangedEventArgs e) => RefreshPreview();

    private void RefreshPreview()
    {
        var mono = CompositeRenderer.ComposeTodo(_items.ToList(), _services.DeviceFont);

        var scale = 1;
        var hostWidth = PreviewHost.ActualWidth;
        var hostHeight = PreviewHost.ActualHeight;
        if (hostWidth > 0 && hostHeight > 0)
        {
            scale = Math.Max(1, (int)Math.Floor(Math.Min(hostWidth / 400.0, hostHeight / 300.0)));
        }

        PreviewImage.Source = ArtworkService.Render(mono, 400, 300, scale);
    }
}

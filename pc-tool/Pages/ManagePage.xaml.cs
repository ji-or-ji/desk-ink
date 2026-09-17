using DeskInk.PcTool.Services;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace DeskInk.PcTool.Pages;

public sealed partial class ManagePage : Page
{
    private readonly AppServices _services = App.Instance.Services;

    public ManagePage()
    {
        InitializeComponent();

        LogList.ItemsSource = _services.Log.Entries;

        _services.Ipc.ConnectionChanged += _ => Refresh();
        _services.Ipc.LessonUpdated += _ => Refresh();
        _services.Serial.StateChanged += _ => Refresh();
        _services.Push.PushStateChanged += Refresh;

        Refresh();
    }

    private void Refresh()
    {
        Ui.Post(() =>
        {
            IpcStatusText.Text = _services.Ipc.IsConnected ? "已连接" : "未连接";
            IpcDetailText.Text = _services.Ipc.IsConnected ? "IPC 正常" : "等待 ClassIsland 启动";

            SerialStatusText.Text = _services.Serial.IsOpen ? "已打开" : "未打开";
            SerialDetailText.Text = _services.Serial.IsOpen
                ? $"{_services.Serial.PortName} @ 115200 8N1"
                : "未选择端口";

            var snapshot = _services.Ipc.LastSnapshot;
            if (snapshot is not null)
            {
                CurrentSubjectText.Text = string.IsNullOrEmpty(snapshot.CurrentName) ? "—" : snapshot.CurrentName;
                CurrentRangeText.Text = snapshot.CurrentTimeRange;
                CurrentLeftText.Text = $"剩余 {snapshot.LeftText}";

                NextSubjectText.Text = string.IsNullOrEmpty(snapshot.NextName) ? "—" : snapshot.NextName;
                NextRangeText.Text = snapshot.NextTimeRange;
                StateText.Text = $"状态：{snapshot.StateText}";
            }

            PushCountText.Text = _services.Push.LastPushTime is null
                ? "已推送 0 条"
                : $"已推送 {_services.Push.PushCount} 条 · {_services.Push.LastPushTime:HH:mm:ss}";
        });
    }

    private void TestPushButton_Click(object sender, RoutedEventArgs e)
    {
        if (_services.Ipc.LastSnapshot is null)
        {
            _services.Log.Warn("暂无课表数据，无法测试推送");
            return;
        }

        _services.Push.PushAll("手动测试");
    }
}

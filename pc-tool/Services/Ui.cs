using Microsoft.UI.Dispatching;

namespace DeskInk.PcTool.Services;

/// <summary>把后台线程的调用切回 UI 线程。</summary>
public static class Ui
{
    public static DispatcherQueue? Dispatcher { get; set; }

    public static void Post(Action action)
    {
        var dispatcher = Dispatcher;
        if (dispatcher is null || dispatcher.HasThreadAccess)
        {
            action();
            return;
        }

        dispatcher.TryEnqueue(() => action());
    }
}

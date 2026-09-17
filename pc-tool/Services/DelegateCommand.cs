using System.Windows.Input;

namespace DeskInk.PcTool.Services;

/// <summary>最小 ICommand 实现，给托盘的 LeftClickCommand 之类用。</summary>
public sealed class DelegateCommand : ICommand
{
    private readonly Action _execute;

    public DelegateCommand(Action execute) => _execute = execute;

    public event EventHandler? CanExecuteChanged
    {
        add { }
        remove { }
    }

    public bool CanExecute(object? parameter) => true;

    public void Execute(object? parameter) => _execute();
}

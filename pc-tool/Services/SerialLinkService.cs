using System.IO.Ports;
using System.Text;

namespace DeskInk.PcTool.Services;

/// <summary>蓝牙 SPP 虚拟 COM 口：枚举、打开、收发 JSON Lines。</summary>
public sealed class SerialLinkService : IDisposable
{
    private const int MaxFrameBytes = 255;

    private static readonly UTF8Encoding Utf8 = new(encoderShouldEmitUTF8Identifier: false);

    private readonly LogService _log;
    private readonly StringBuilder _receiveBuffer = new();
    private readonly object _sync = new();

    private SerialPort? _port;

    public SerialLinkService(LogService log)
    {
        _log = log;
    }

    public bool IsOpen => _port?.IsOpen == true;

    public string? PortName { get; private set; }

    public event Action<bool>? StateChanged;

    /// <summary>收到设备发来的一整行（不含换行）。</summary>
    public event Action<string>? LineReceived;

    public event Action<string>? LineSent;

    public static string[] GetPortNames()
    {
        try
        {
            var names = SerialPort.GetPortNames();
            Array.Sort(names, StringComparer.OrdinalIgnoreCase);
            return names;
        }
        catch
        {
            return Array.Empty<string>();
        }
    }

    public bool Open(string portName, int baudRate = 115200)
    {
        Close();

        try
        {
            var port = new SerialPort(portName, baudRate, Parity.None, 8, StopBits.One)
            {
                NewLine = "\n",
                ReadTimeout = 500,
                WriteTimeout = 1000,
                DtrEnable = true,
                RtsEnable = true,
                Encoding = Utf8
            };

            port.DataReceived += OnDataReceived;
            port.Open();

            _port = port;
            PortName = portName;
            _log.Info($"串口已打开：{portName} @ {baudRate} 8N1");
            StateChanged?.Invoke(true);
            return true;
        }
        catch (Exception ex)
        {
            _log.Error($"打开串口 {portName} 失败：{ex.Message}");
            return false;
        }
    }

    public void Close()
    {
        var port = _port;
        if (port is null)
        {
            return;
        }

        _port = null;
        PortName = null;

        try
        {
            port.DataReceived -= OnDataReceived;
            if (port.IsOpen)
            {
                port.Close();
            }
        }
        catch
        {
            // 忽略关闭异常。
        }
        finally
        {
            port.Dispose();
            _log.Info("串口已关闭");
            StateChanged?.Invoke(false);
        }
    }

    public bool SendLine(string line)
    {
        var port = _port;
        if (port?.IsOpen != true)
        {
            _log.Warn("串口未打开，消息被丢弃");
            return false;
        }

        try
        {
            var bytes = Utf8.GetBytes(line + "\n");
            if (bytes.Length > MaxFrameBytes)
            {
                _log.Warn($"帧长 {bytes.Length} 字节，超过 {MaxFrameBytes} 字节上限");
            }

            lock (_sync)
            {
                port.Write(bytes, 0, bytes.Length);
            }

            LineSent?.Invoke(line);
            return true;
        }
        catch (Exception ex)
        {
            _log.Error($"串口写入失败：{ex.Message}");
            Close();
            return false;
        }
    }

    private void OnDataReceived(object sender, SerialDataReceivedEventArgs e)
    {
        var port = _port;
        if (port is null)
        {
            return;
        }

        try
        {
            var available = port.BytesToRead;
            if (available <= 0)
            {
                return;
            }

            var buffer = new byte[available];
            var read = port.Read(buffer, 0, available);
            var text = Utf8.GetString(buffer, 0, read);

            List<string> lines = new();
            lock (_sync)
            {
                _receiveBuffer.Append(text);
                while (true)
                {
                    var content = _receiveBuffer.ToString();
                    var index = content.IndexOf('\n');
                    if (index < 0)
                    {
                        break;
                    }

                    var line = content[..index].TrimEnd('\r');
                    _receiveBuffer.Remove(0, index + 1);
                    if (line.Length > 0)
                    {
                        lines.Add(line);
                    }
                }

                if (_receiveBuffer.Length > 4096)
                {
                    _receiveBuffer.Clear();
                }
            }

            foreach (var line in lines)
            {
                LineReceived?.Invoke(line);
            }
        }
        catch (Exception ex)
        {
            _log.Warn($"串口读取失败：{ex.Message}");
        }
    }

    public void Dispose() => Close();
}

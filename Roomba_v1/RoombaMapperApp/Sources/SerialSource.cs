using System.IO.Ports;
using System.Text;

namespace RoombaMapper.Sources;

/// <summary>
/// Reads telemetry from a COM port. Use this with the TinyScreen+ plugged in by USB,
/// which is the easiest way to test before Bluetooth is working.
/// </summary>
public sealed class SerialSource : DataSourceBase
{
    private readonly string _portName;
    private readonly int _baud;
    private SerialPort? _port;

    public SerialSource(string portName, int baud = 115200)
    {
        _portName = portName;
        _baud = baud;
    }

    public override string Name => $"USB Serial ({_portName})";

    public override Task StartAsync()
    {
        var port = new SerialPort(_portName, _baud)
        {
            Encoding = Encoding.ASCII,
            DtrEnable = true, // the SAMD21 USB serial only sends once DTR is set
            RtsEnable = true
        };
        port.DataReceived += (_, _) =>
        {
            try
            {
                if (port.IsOpen) OnText(port.ReadExisting());
            }
            catch (Exception ex)
            {
                OnStatus("Serial error: " + ex.Message);
            }
        };
        port.Open();
        _port = port;
        OnStatus($"Connected to {_portName}");
        return Task.CompletedTask;
    }

    public override void Stop()
    {
        var port = _port;
        _port = null;
        if (port == null) return;
        try { port.Close(); } catch { /* ignore */ }
        port.Dispose();
    }
}

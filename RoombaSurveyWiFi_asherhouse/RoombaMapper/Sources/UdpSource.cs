using System.Net.Sockets;
using System.Text;

namespace RoombaMapper.Sources;

/// <summary>
/// Listens for telemetry sent over Wi-Fi as UDP packets (one line per packet).
/// This is for when the Wi-Fi TinyShield arrives. Windows Firewall may ask for
/// permission the first time; allow it on private networks.
/// </summary>
public sealed class UdpSource : DataSourceBase
{
    private readonly int _port;
    private UdpClient? _udp;
    private CancellationTokenSource? _cts;
    private System.Net.IPEndPoint? _robot;   // where commands go (the robot's IP and port)

    public UdpSource(int port) => _port = port;

    public override string Name => $"Wi-Fi UDP (port {_port})";

    public override Task StartAsync()
    {
        var udp = new UdpClient(_port);
        var cts = new CancellationTokenSource();
        _udp = udp;
        _cts = cts;

        _ = Task.Run(async () =>
        {
            while (!cts.IsCancellationRequested)
            {
                try
                {
                    var result = await udp.ReceiveAsync(cts.Token);
                    if (!Equals(result.RemoteEndPoint, _robot))
                    {
                        _robot = result.RemoteEndPoint;
                        OnStatus($"Receiving from robot at {_robot.Address}");
                    }
                    var text = Encoding.ASCII.GetString(result.Buffer);
                    if (!text.EndsWith('\n')) text += "\n";
                    OnText(text);
                }
                catch (OperationCanceledException) { break; }
                catch (ObjectDisposedException) { break; }
                catch (Exception ex) { OnStatus("UDP error: " + ex.Message); }
            }
        });

        OnStatus($"Listening for Wi-Fi data on UDP port {_port}. Make sure this PC is on the same Wi-Fi network as the robot.");
        return Task.CompletedTask;
    }

    public override bool CanSend => _robot != null && _udp != null;

    public override void Send(string line)
    {
        var udp = _udp;
        var robot = _robot;
        if (udp == null || robot == null) return;
        var bytes = Encoding.ASCII.GetBytes(line.EndsWith('\n') ? line : line + "\n");
        try { udp.Send(bytes, bytes.Length, robot); }
        catch (Exception ex) { OnStatus("Could not send to robot: " + ex.Message); }
    }

    public override void Stop()
    {
        _cts?.Cancel();
        _udp?.Dispose();
        _cts?.Dispose();
        _udp = null;
        _cts = null;
        _robot = null;
    }
}

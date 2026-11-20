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

    private readonly string? _deviceIp;

    /// <param name="deviceIp">The glove/robot's IP if known (shown on its screen), else null</param>
    public UdpSource(int port, string? deviceIp = null)
    {
        _port = port;
        _deviceIp = string.IsNullOrWhiteSpace(deviceIp) ? null : deviceIp.Trim();
    }

    public override string Name => $"Wi-Fi UDP (port {_port})";

    public override Task StartAsync()
    {
        var udp = new UdpClient(_port) { EnableBroadcast = true };
        var cts = new CancellationTokenSource();
        _udp = udp;
        _cts = cts;

        // Until the device is heard from: knock every 2 s (broadcast on every network this PC
        // is on, plus the device's IP if typed in). Any packet from the app tells the device
        // where the app is, so it can send straight here. Phone hotspots often drop the
        // device's own broadcasts, so without this the two may never find each other.
        _ = Task.Run(async () =>
        {
            var ping = Encoding.ASCII.GetBytes("PING\n");
            while (!cts.IsCancellationRequested)
            {
                if (_robot == null)
                {
                    foreach (var target in KnockTargets())
                        try { await udp.SendAsync(ping, ping.Length, new System.Net.IPEndPoint(target, _port + 1)); }
                        catch { /* a network may refuse broadcasts: try the others */ }
                }
                try { await Task.Delay(2000, cts.Token); } catch { break; }
            }
        });

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

    private IEnumerable<System.Net.IPAddress> KnockTargets()
    {
        if (_deviceIp != null && System.Net.IPAddress.TryParse(_deviceIp, out var ip)) yield return ip;
        yield return System.Net.IPAddress.Broadcast;
        foreach (var ni in System.Net.NetworkInformation.NetworkInterface.GetAllNetworkInterfaces())
        {
            if (ni.OperationalStatus != System.Net.NetworkInformation.OperationalStatus.Up) continue;
            foreach (var ua in ni.GetIPProperties().UnicastAddresses)
            {
                if (ua.Address.AddressFamily != AddressFamily.InterNetwork || ua.IPv4Mask == null) continue;
                var a = ua.Address.GetAddressBytes();
                var m = ua.IPv4Mask.GetAddressBytes();
                for (int i = 0; i < 4; i++) a[i] = (byte)(a[i] | ~m[i]);
                yield return new System.Net.IPAddress(a);       // e.g. 172.20.10.15 for a hotspot
            }
        }
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

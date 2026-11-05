using System.Globalization;
using System.Text;

namespace RoombaMapper;

/// <summary>
/// One reading from the robot. Positions are in millimetres, robot starts at (0,0)
/// facing +X. Heading is in degrees, counter-clockwise positive.
/// Rssi is null when the robot has no Wi-Fi reading yet.
/// </summary>
public readonly record struct Telemetry(
    long TimeMs, double X, double Y, double HeadingDeg, double DistanceMm, double? Rssi, bool? Ir = null);

/// <summary>An obstacle the IR sensor detected, at an estimated position (mm).</summary>
public readonly record struct ObstacleEvent(long TimeMs, double X, double Y);

/// <summary>
/// Parses lines of the form:
///   P,&lt;time_ms&gt;,&lt;x_mm&gt;,&lt;y_mm&gt;,&lt;heading_deg&gt;,&lt;distance_mm&gt;,&lt;rssi_dbm or NA&gt;[,&lt;ir 0/1&gt;]
/// e.g. "P,12500,1034,-20,90,2310,-57,0"
/// </summary>
public static class TelemetryParser
{
    private static readonly CultureInfo Inv = CultureInfo.InvariantCulture;

    public static bool TryParse(string line, out Telemetry t)
    {
        t = default;
        var p = line.Trim().Split(',');
        if (p.Length < 7 || p[0] != "P") return false;

        if (!long.TryParse(p[1], NumberStyles.Integer, Inv, out var ms)) return false;
        if (!double.TryParse(p[2], NumberStyles.Float, Inv, out var x)) return false;
        if (!double.TryParse(p[3], NumberStyles.Float, Inv, out var y)) return false;
        if (!double.TryParse(p[4], NumberStyles.Float, Inv, out var h)) return false;
        if (!double.TryParse(p[5], NumberStyles.Float, Inv, out var d)) return false;

        double? rssi = null;
        if (double.TryParse(p[6], NumberStyles.Float, Inv, out var r) && r < 0 && r > -127)
            rssi = r;

        bool? ir = null;
        if (p.Length >= 8 && int.TryParse(p[7], NumberStyles.Integer, Inv, out var irVal))
            ir = irVal != 0;

        t = new Telemetry(ms, x, y, h, d, rssi, ir);
        return true;
    }

    /// <summary>Parses "O,&lt;time_ms&gt;,&lt;x_mm&gt;,&lt;y_mm&gt;".</summary>
    public static bool TryParseObstacle(string line, out ObstacleEvent o)
    {
        o = default;
        var p = line.Trim().Split(',');
        if (p.Length < 4 || p[0] != "O") return false;
        if (!long.TryParse(p[1], NumberStyles.Integer, Inv, out var ms)) return false;
        if (!double.TryParse(p[2], NumberStyles.Float, Inv, out var x)) return false;
        if (!double.TryParse(p[3], NumberStyles.Float, Inv, out var y)) return false;
        o = new ObstacleEvent(ms, x, y);
        return true;
    }
}

/// <summary>
/// Bluetooth LE delivers data in small chunks (often 20 bytes), so a line can be split
/// across several packets. This joins chunks back into complete lines.
/// </summary>
public sealed class LineAssembler
{
    private readonly StringBuilder _sb = new();

    public List<string> Feed(string chunk)
    {
        var lines = new List<string>();
        foreach (var c in chunk)
        {
            if (c == '\n')
            {
                if (_sb.Length > 0) lines.Add(_sb.ToString());
                _sb.Clear();
            }
            else if (c != '\r')
            {
                _sb.Append(c);
                if (_sb.Length > 256) _sb.Clear(); // garbage protection
            }
        }
        return lines;
    }
}

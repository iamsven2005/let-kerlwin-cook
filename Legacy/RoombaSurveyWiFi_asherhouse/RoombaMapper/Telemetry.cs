using System.Globalization;
using System.Text;

namespace RoombaMapper;

/// <summary>
/// One reading from the robot. Positions are in millimetres, robot starts at (0,0)
/// facing +X. Heading is in degrees, counter-clockwise positive.
/// Rssi is null when the robot has no Wi-Fi reading yet.
/// </summary>
public readonly record struct Telemetry(
    long TimeMs, double X, double Y, double HeadingDeg, double DistanceMm, double? Rssi,
    bool? Ir = null, bool? IrLeft = null, bool? IrRight = null,
    string? State = null, string? Mode = null, int? SpeedPct = null, string? AccessPoint = null,
    string? Sensors = null);

/// <summary>
/// One Wi-Fi reading from the robot's cache: "R,seq,mode,rssi,min,max,age,x_mm,y_mm".
/// Mode 'F' = single reading, 'S' = average (min/max = its spread). AgeMs = how long ago
/// it was taken. X/Y = where the robot was when it was taken.
/// </summary>
public readonly record struct WifiReading(int Seq, char Mode, int Rssi, int Min, int Max, long AgeMs, double X, double Y);

/// <summary>Reply from the robot confirming a command ("K,&lt;id&gt;,&lt;text&gt;").</summary>
public readonly record struct CommandAck(long Id, string Text);

/// <summary>
/// A point on an obstacle's edge (mm). ShapeId groups the points of one traced outline;
/// 0 means a single hit that was not traced.
/// </summary>
public readonly record struct ObstacleEvent(long TimeMs, double X, double Y, int ShapeId = 0, double? FacingDeg = null);

/// <summary>A traced outline finished. Kind: 1 = object, 2 = room wall, 0 = incomplete.</summary>
public readonly record struct ShapeEvent(long TimeMs, int ShapeId, int Kind);

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

        // Newer robot firmware adds: <state>,<FAST|SLOW>,<speed%> after the IR flag.
        // Older 3-sensor firmware put left/right IR flags there instead.
        bool hasState = p.Length > 8 && !int.TryParse(p[8], NumberStyles.Integer, Inv, out _);
        if (hasState)
        {
            int? speed = p.Length > 10 && int.TryParse(p[10], NumberStyles.Integer, Inv, out var sp) ? sp : null;
            // ...then <AP>,<sensors F|FL|FR|F+P>,<left ir>,<right ir>
            t = new Telemetry(ms, x, y, h, d, rssi, Flag(p, 7), Flag(p, 13), Flag(p, 14),
                              p[8], p.Length > 9 ? p[9] : null, speed,
                              p.Length > 11 ? p[11].Trim() : null,
                              p.Length > 12 ? p[12].Trim() : null);
        }
        else
        {
            t = new Telemetry(ms, x, y, h, d, rssi, Flag(p, 7), Flag(p, 8), Flag(p, 9));
        }
        return true;
    }

    private static bool? Flag(string[] p, int i) =>
        p.Length > i && int.TryParse(p[i], NumberStyles.Integer, Inv, out var v) ? v != 0 : null;

    /// <summary>Parses "O,&lt;time_ms&gt;,&lt;x_mm&gt;,&lt;y_mm&gt;[,&lt;shape_id&gt;]".</summary>
    public static bool TryParseObstacle(string line, out ObstacleEvent o)
    {
        o = default;
        var p = line.Trim().Split(',');
        if (p.Length < 4 || p[0] != "O") return false;
        if (!long.TryParse(p[1], NumberStyles.Integer, Inv, out var ms)) return false;
        if (!double.TryParse(p[2], NumberStyles.Float, Inv, out var x)) return false;
        if (!double.TryParse(p[3], NumberStyles.Float, Inv, out var y)) return false;
        int shape = 0;
        if (p.Length >= 5) int.TryParse(p[4], NumberStyles.Integer, Inv, out shape);
        double? facing = p.Length >= 6 && double.TryParse(p[5], NumberStyles.Float, Inv, out var f) ? f : null;
        o = new ObstacleEvent(ms, x, y, shape, facing);
        return true;
    }

    public static bool TryParseReading(string line, out WifiReading r)
    {
        r = default;
        var p = line.Trim().Split(',');
        if (p.Length < 9 || p[0] != "R" || p[2].Length != 1) return false;
        if (!int.TryParse(p[1], NumberStyles.Integer, Inv, out var seq)) return false;
        if (!int.TryParse(p[3], NumberStyles.Integer, Inv, out var rssi)) return false;
        if (!int.TryParse(p[4], NumberStyles.Integer, Inv, out var min)) return false;
        if (!int.TryParse(p[5], NumberStyles.Integer, Inv, out var max)) return false;
        if (!long.TryParse(p[6], NumberStyles.Integer, Inv, out var age)) return false;
        if (!double.TryParse(p[7], NumberStyles.Float, Inv, out var x)) return false;
        if (!double.TryParse(p[8], NumberStyles.Float, Inv, out var y)) return false;
        r = new WifiReading(seq, p[2][0], rssi, min, max, age, x, y);
        return true;
    }

    /// <summary>Parses "M,&lt;text&gt;" (a message from the robot).</summary>
    public static bool TryParseMessage(string line, out string text)
    {
        text = "";
        if (!line.StartsWith("M,")) return false;
        text = line[2..].Trim();
        return true;
    }

    /// <summary>Parses "K,&lt;id&gt;,&lt;text&gt;" (command acknowledged).</summary>
    public static bool TryParseAck(string line, out CommandAck ack)
    {
        ack = default;
        var p = line.Trim().Split(',', 3);
        if (p.Length < 2 || p[0] != "K") return false;
        if (!long.TryParse(p[1], NumberStyles.Integer, Inv, out var id)) return false;
        ack = new CommandAck(id, p.Length > 2 ? p[2] : "");
        return true;
    }

    /// <summary>Parses "S,&lt;time_ms&gt;,&lt;shape_id&gt;,&lt;kind&gt;".</summary>
    public static bool TryParseShape(string line, out ShapeEvent e)
    {
        e = default;
        var p = line.Trim().Split(',');
        if (p.Length < 4 || p[0] != "S") return false;
        if (!long.TryParse(p[1], NumberStyles.Integer, Inv, out var ms)) return false;
        if (!int.TryParse(p[2], NumberStyles.Integer, Inv, out var id)) return false;
        if (!int.TryParse(p[3], NumberStyles.Integer, Inv, out var kind)) return false;
        e = new ShapeEvent(ms, id, kind);
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

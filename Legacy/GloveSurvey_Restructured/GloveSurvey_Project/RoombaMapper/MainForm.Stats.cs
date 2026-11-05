namespace RoombaMapper;

/// <summary>
/// Feature "Stats panel" (Features.StatsPanel): the values in the right-hand panel -
/// state, distance, position, heading, Wi-Fi signal, access point, IR, obstacles, readings.
/// </summary>
public sealed partial class MainForm
{
    private const double WheelCircumferenceMm = Math.PI * 40.0; // 40 mm wheel

    private readonly Label _distLbl = ValueLabel(), _turnsLbl = ValueLabel(), _posLbl = ValueLabel(),
        _headLbl = ValueLabel(), _rssiLbl = ValueLabel(), _irLbl = ValueLabel(), _obstLbl = ValueLabel(),
        _countLbl = ValueLabel(), _stateLbl = ValueLabel(), _apLbl = ValueLabel();
    private int _withSignal;

    private void BuildStatsPanel()
    {
        AddStat(_side, "Robot state", _stateLbl);
        AddStat(_side, "Distance travelled", _distLbl);
        AddStat(_side, "Wheel rotations (40 mm wheel)", _turnsLbl);
        AddStat(_side, "Position", _posLbl);
        AddStat(_side, "Heading", _headLbl);
        AddStat(_side, "Wi-Fi signal", _rssiLbl);
        AddStat(_side, "Access point (MAC end, channel)", _apLbl);
        AddStat(_side, "IR sensors: something there?", _irLbl);
        AddStat(_side, "Obstacles", _obstLbl);
        AddStat(_side, "Readings", _countLbl);
    }

    private static void AddStat(Control parent, string caption, Label value)
    {
        parent.Controls.Add(new Label { Text = caption, AutoSize = true, ForeColor = Color.Gray });
        parent.Controls.Add(value);
    }

    private void StatsOnTelemetry(Telemetry t)
    {
        if (t.Rssi.HasValue) _withSignal++;

        _distLbl.Text = $"{t.DistanceMm / 1000:0.00} m";
        _turnsLbl.Text = $"{t.DistanceMm / WheelCircumferenceMm:0.0}";
        _posLbl.Text = $"x {t.X / 1000:0.00}  y {t.Y / 1000:0.00} m";
        _headLbl.Text = $"{t.HeadingDeg:0}°";
        if (t.Rssi is double r)   // keep showing the last reading until a new one arrives
        {
            string quality = r >= -50 ? "Excellent" : r >= -70 ? "Good" : "Weak";
            _rssiLbl.Text = $"{r:0} dBm  {quality}";
            _rssiLbl.ForeColor = _map.ColorFor(r);
        }
        if (t.Rssi.HasValue) _countLbl.Text = $"{_withSignal} Wi-Fi readings";

        if (t.AccessPoint != null) _apLbl.Text = t.AccessPoint;

        if (t.State != null)
        {
            _stateLbl.Text = $"{t.State}  {t.Mode}  {t.SpeedPct}%";
            _stateLbl.ForeColor = t.State == "PAUSED" ? Color.DarkGoldenrod : Color.SteelBlue;
        }

        if (t.Ir is null)
        {
            _irLbl.Text = "no sensor";
            _irLbl.ForeColor = Color.Gray;
        }
        else
        {
            static string F(bool? b) => b == true ? "YES" : "no";
            _irLbl.Text = t.Sensors switch
            {
                "FL" => $"Front {F(t.Ir)} · Left {F(t.IrLeft)}",
                "FR" => $"Front {F(t.Ir)} · Right {F(t.IrRight)}",
                _ => F(t.Ir)
            };
            bool any = t.Ir == true || t.IrLeft == true || t.IrRight == true;
            _irLbl.ForeColor = any ? Color.Firebrick : Color.SeaGreen;
        }
    }

    private void StatsOnReading(WifiReading r)
    {
        _withSignal++;
        string quality = r.Rssi >= -50 ? "Excellent" : r.Rssi >= -70 ? "Good" : "Weak";
        _rssiLbl.Text = r.Mode == 'S' ? $"{r.Rssi} dBm  {quality} (avg)" : $"{r.Rssi} dBm  {quality}";
        _rssiLbl.ForeColor = _map.ColorFor(r.Rssi);
        _countLbl.Text = $"{_withSignal} Wi-Fi readings";
    }

    /// <summary>Also called by the core and the mission when the mapped world changes.</summary>
    private void UpdateShapeLabel() =>
        _obstLbl.Text = $"{_world.Obstacles.Count(o => o.Blocks)} mapped, " +
                        $"{_world.Seeds.Count(s => s.State == RoombaMapper.Core.SeedState.Pending)} seeds left";

    private void StatsClear()
    {
        _withSignal = 0;
        _distLbl.Text = "0.00 m";
        _turnsLbl.Text = "0.0";
        _posLbl.Text = "x 0.00  y 0.00 m";
        _headLbl.Text = "0°";
        _rssiLbl.Text = "no reading";
        _rssiLbl.ForeColor = Color.Gray;
        _countLbl.Text = "0";
        _irLbl.Text = "no sensor";
        _irLbl.ForeColor = Color.Gray;
        _obstLbl.Text = "0";
        _stateLbl.Text = "unknown";
        _apLbl.Text = "unknown";
        _apLbl.ForeColor = Color.SteelBlue;
        _stateLbl.ForeColor = Color.Gray;
    }
}

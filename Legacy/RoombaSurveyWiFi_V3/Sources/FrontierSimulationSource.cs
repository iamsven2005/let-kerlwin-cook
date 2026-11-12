using System.Globalization;

namespace RoombaMapper.Sources;

/// <summary>Telemetry from the virtual four-IR frontier mapper; never sends hardware commands.</summary>
public sealed class FrontierSimulationSource : DataSourceBase
{
    private readonly object _lock = new();
    private readonly FrontierSimulation _sim = new();
    private System.Threading.Timer? _timer;
    private int _ticks;
    public override string Name => "Frontier simulation (no hardware)";
    public override Task StartAsync()
    {
        OnText("F,sim\n");
        OnStatus("Simulation: exploring with four virtual IR sensors");
        _timer = new System.Threading.Timer(Tick, null, 0, 50);
        return Task.CompletedTask;
    }
    public override void Stop() { lock (_lock) { _timer?.Dispose(); _timer = null; } }
    private void Tick(object? state)
    {
        lock (_lock)
        {
            if (_timer == null || _sim.Done) return;
            _sim.Step(); _ticks++;
            var p = _sim.Position;
            var line = string.Format(CultureInfo.InvariantCulture,
                "P,{0},{1},{2},{3:0},{4},{5:0}\n", _ticks * 50, (p.x-1)*300, (p.y-16)*300,
                _sim.HeadingDegrees, _sim.Steps*300, _sim.Rssi);
            OnText(line);
            if (_ticks % 20 == 0 || _sim.Done)
            {
                string sensors = string.Join("|", _sim.IrReadings.Select((r,i) => $"{new[]{"FL","FR","L","R"}[i]}: {(r.HasValue ? "obstacle" : "clear")}"));
                OnText($"D,sim,{_sim.Phase}|Visited {_sim.VisitedCount}|{sensors}\n");
            }
            if (_sim.Done) OnStatus($"Simulation DONE: weakest sampled Wi-Fi reached; {_sim.VisitedCount} cells visited.");
        }
    }
}

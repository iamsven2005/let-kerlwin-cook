namespace RoombaMapper;

/// <summary>
/// One app feature: a name, its on/off switch (Features.cs) and the hooks it wants.
/// Hooks left null are skipped. The feature table is in MainForm.Features.cs.
/// </summary>
public sealed class AppFeature
{
    public AppFeature(string name, bool enabled)
    {
        Name = name;
        Enabled = enabled;
    }

    public string Name { get; }
    public bool Enabled { get; }

    /// <summary>Once, while the window is built: add rows, buttons, labels.</summary>
    public Action? OnBuild { get; init; }
    public Action<Telemetry>? OnTelemetry { get; init; }
    public Action<WifiReading>? OnReading { get; init; }
    public Action<ObstacleEvent>? OnObstacle { get; init; }
    /// <summary>F (feature list) and D (debug page) lines from the glove.</summary>
    public Action<string>? OnFeatureLine { get; init; }
    /// <summary>"Clear map" (and start-up).</summary>
    public Action? OnClear { get; init; }
    public Action? OnDisconnected { get; init; }
}

/// <summary>
/// The app's main logic loop. Everything that arrives from the glove or robot is parsed
/// once (DataSourceBase), handled by the window's core (MainForm.cs), then passed to
/// every enabled feature in table order.
/// </summary>
public sealed class FeatureRunner
{
    private readonly List<AppFeature> _all;
    private readonly List<AppFeature> _on;

    public FeatureRunner(IEnumerable<AppFeature> features)
    {
        _all = features.ToList();
        _on = _all.Where(f => f.Enabled).ToList();
    }

    public IReadOnlyList<AppFeature> All => _all;
    public IReadOnlyList<AppFeature> Enabled => _on;

    public void DispatchBuild() { foreach (var f in _on) f.OnBuild?.Invoke(); }
    public void DispatchTelemetry(Telemetry t) { foreach (var f in _on) f.OnTelemetry?.Invoke(t); }
    public void DispatchReading(WifiReading r) { foreach (var f in _on) f.OnReading?.Invoke(r); }
    public void DispatchObstacle(ObstacleEvent o) { foreach (var f in _on) f.OnObstacle?.Invoke(o); }
    public void DispatchFeatureLine(string line) { foreach (var f in _on) f.OnFeatureLine?.Invoke(line); }
    public void DispatchClear() { foreach (var f in _on) f.OnClear?.Invoke(); }
    public void DispatchDisconnected() { foreach (var f in _on) f.OnDisconnected?.Invoke(); }
}

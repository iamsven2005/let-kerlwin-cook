namespace RoombaMapper.Sources;

/// <summary>
/// Base class for everything that can feed telemetry into the app:
/// simulation, USB serial, Bluetooth LE, and Wi-Fi (UDP).
/// Events are raised on background threads; the form marshals them to the UI thread.
/// </summary>
public abstract class DataSourceBase : IDisposable
{
    private readonly LineAssembler _assembler = new();
    private readonly object _textLock = new();

    public event Action<Telemetry>? TelemetryReceived;
    public event Action<ObstacleEvent>? ObstacleDetected;
    public event Action<string>? Status;
    public event Action<string>? RawLine;

    public abstract string Name { get; }
    public abstract Task StartAsync();
    public abstract void Stop();

    /// <summary>Feed raw text from the transport. Complete lines are parsed.</summary>
    protected void OnText(string chunk)
    {
        List<string> lines;
        lock (_textLock) lines = _assembler.Feed(chunk);

        foreach (var line in lines)
        {
            RawLine?.Invoke(line);
            if (TelemetryParser.TryParse(line, out var t))
                TelemetryReceived?.Invoke(t);
            else if (TelemetryParser.TryParseObstacle(line, out var o))
                ObstacleDetected?.Invoke(o);
        }
    }

    protected void OnStatus(string message) => Status?.Invoke(message);

    public void Dispose()
    {
        Stop();
        GC.SuppressFinalize(this);
    }
}

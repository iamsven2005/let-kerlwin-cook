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
    public event Action<ShapeEvent>? ShapeFinished;
    public event Action<string>? Status;
    public event Action<string>? RobotMessage;
    public event Action<CommandAck>? CommandAcknowledged;
    public event Action<WifiReading>? ReadingReceived;
    /// <summary>Scan (W) and side-look (Q) results from the robot's primitives.</summary>
    public event Action<string>? ResultLine;
    public event Action<string>? RawLine;
    public event Action<string>? FeatureLine;

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
            if (line.StartsWith("D,"))
            {
                FeatureLine?.Invoke(line);
                continue;
            }
            if (line.StartsWith("F,")) FeatureLine?.Invoke(line);
            if (line.StartsWith("W,") || line.StartsWith("Q,"))
            {
                RawLine?.Invoke(line);
                ResultLine?.Invoke(line);
                continue;
            }
            if (TelemetryParser.TryParseReading(line, out var reading))
            {
                ReadingReceived?.Invoke(reading);   // not logged: there can be many
                continue;
            }
            RawLine?.Invoke(line);
            if (TelemetryParser.TryParse(line, out var t))
                TelemetryReceived?.Invoke(t);
            else if (TelemetryParser.TryParseObstacle(line, out var o))
                ObstacleDetected?.Invoke(o);
            else if (TelemetryParser.TryParseShape(line, out var sh))
                ShapeFinished?.Invoke(sh);
            else if (TelemetryParser.TryParseAck(line, out var ack))
                CommandAcknowledged?.Invoke(ack);
            else if (TelemetryParser.TryParseMessage(line, out var msg))
                RobotMessage?.Invoke(msg);
        }
    }

    protected void OnStatus(string message) => Status?.Invoke(message);

    /// <summary>True when commands can be sent to the robot through this source.</summary>
    public virtual bool CanSend => false;

    /// <summary>Send one command line to the robot.</summary>
    public virtual void Send(string line) { }

    public void Dispose()
    {
        Stop();
        GC.SuppressFinalize(this);
    }
}

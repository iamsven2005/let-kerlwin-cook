using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
using RoombaMapper.Core;

namespace RoombaMapper;

/// <summary>
/// Draws a floor-plan style grid, the robot's path, the robot itself, and a Wi-Fi
/// heat map built from signal readings. Scroll to zoom, drag to pan.
/// </summary>
public sealed partial class MapView : Control
{
    // Heat map settings
    private const double BinMm = 200;          // readings are averaged into 20 cm squares
    private const double CellMm = 100;         // heat map drawn at 10 cm resolution
    private const double HeatRadiusMm = 700;   // how far each reading "spreads"
    private const double RobotRadiusMm = 90;

    private readonly List<Telemetry> _samples = new();
    private readonly List<ObstacleEvent> _obstacles = new();
    private readonly List<WifiReading> _readings = new();
    public IReadOnlyList<WifiReading> Readings => _readings;
    private readonly Dictionary<int, List<ObstacleEvent>> _shapePoints = new();
    private readonly Dictionary<int, int> _shapeKinds = new();
    private const double OutlineGapMm = 250; // points further apart than this are not joined
    private readonly Dictionary<(int, int), (double Sum, int Count)> _bins = new();

    private Bitmap? _heat;
    private double _heatMinX, _heatMaxY;
    private int _heatW, _heatH;
    private bool _heatDirty;
    private DateTime _lastHeatBuild = DateTime.MinValue;
    private readonly System.Windows.Forms.Timer _refresh = new() { Interval = 300 };

    // View state
    private double _scale = 0.15;               // pixels per mm
    private double _centerX, _centerY;           // world mm at the middle of the control
    private bool _autoFit = true;
    private bool _dragging;
    private Point _dragStart;
    private double _dragCX, _dragCY;

    private double _minX, _maxX, _minY, _maxY;
    private bool _hasBounds;

    private void Grow(double x, double y)
    {
        if (!_hasBounds) { _minX = _maxX = x; _minY = _maxY = y; _hasBounds = true; return; }
        _minX = Math.Min(_minX, x); _maxX = Math.Max(_maxX, x);
        _minY = Math.Min(_minY, y); _maxY = Math.Max(_maxY, y);
    }

    private bool _showHeat = true, _showPath = true;
    public bool ShowHeat { get => _showHeat; set { _showHeat = value; Invalidate(); } }
    public bool ShowPath { get => _showPath; set { _showPath = value; Invalidate(); } }

    /// <summary>Signal at or above this is drawn fully green.</summary>
    public double RssiStrong { get; set; } = -45;
    /// <summary>Signal at or below this is drawn fully red.</summary>
    public double RssiWeak { get; set; } = -80;

    public IReadOnlyList<Telemetry> Samples => _samples;

    private (double X, double Y)? _target;
    /// <summary>Where the robot has been sent (drawn as a target), or null.</summary>
    public (double X, double Y)? Target { get => _target; set { _target = value; Invalidate(); } }

    /// <summary>Converts a point on the control to map coordinates (mm).</summary>
    public (double X, double Y) ScreenToWorld(Point p) => S2W(p.X, p.Y);
    public IReadOnlyList<ObstacleEvent> Obstacles => _obstacles;
    public IReadOnlyDictionary<int, List<ObstacleEvent>> ShapePoints => _shapePoints;
    public IReadOnlyDictionary<int, int> ShapeKinds => _shapeKinds;

    /// <summary>Which traced outline is the room. Only that one is drawn as room walls.</summary>
    public int? RoomShapeId { get => _roomShapeId; set { _roomShapeId = value; Invalidate(); } }
    private int? _roomShapeId;

    // A closed outline the robot went round the INSIDE of, but which isn't the room
    // (e.g. a phase 2 trace that went the wrong way): shown as an unfinished outline
    private int KindOf(int id)
    {
        _shapeKinds.TryGetValue(id, out int kind);
        return kind == 2 && id != _roomShapeId ? 0 : kind;
    }

    private readonly Dictionary<int, List<(double X, double Y)>> _corners = new();
    /// <summary>Simplified outline (corners) of a traced shape, drawn with wall lengths.</summary>
    public void SetCorners(int shapeId, List<(double X, double Y)> corners)
    {
        _corners[shapeId] = corners;
        Invalidate();
    }
    public IReadOnlyDictionary<int, List<(double X, double Y)>> Corners => _corners;

    // ---------------------------------------------------------------- the mapped world (Core)
    private WorldMap? _world;
    /// <summary>The room/obstacle map built by the mapping phases (drawn with labels).</summary>
    public WorldMap? World { get => _world; set { _world = value; Invalidate(); } }
    public RobotConfig? Cfg { get; set; }
    private Bitmap? _worldCov;
    private long _worldCovKey = -1;
    private double _worldCovX0, _worldCovYTop;

    /// <summary>Make sure the view's bounds include the mapped room and obstacles.</summary>
    public void GrowToWorld()
    {
        if (_world == null) return;
        if (_world.Room is { } r) foreach (var c in r.Corners) Grow(c[0], c[1]);
        foreach (var o in _world.Obstacles) foreach (var c in o.Footprint) Grow(c[0], c[1]);
        if (_autoFit) FitInternal();
    }

    private Bitmap? _coverage;
    private double _covX0, _covYTop, _covCell;
    /// <summary>Done (green) / skipped (amber) areas during phases 2 and 3, or null.</summary>
    public void SetCoverage(Bitmap? image, double x0 = 0, double yTop = 0, double cellMm = 50)
    {
        _coverage?.Dispose();
        _coverage = image;
        _covX0 = x0; _covYTop = yTop; _covCell = cellMm;
        Invalidate();
    }

    private Bitmap? _noGo;
    private double _noGoX0, _noGoYTop, _noGoCell;
    /// <summary>Shows where routes can't go (from the planner), or null to hide it.</summary>
    public void SetNoGo(Bitmap? image, double x0 = 0, double yTop = 0, double cellMm = 50)
    {
        _noGo?.Dispose();
        _noGo = image;
        _noGoX0 = x0; _noGoYTop = yTop; _noGoCell = cellMm;
        Invalidate();
    }

    private List<(double X, double Y)>? _plannedPath;
    /// <summary>The route the robot is going to follow (drawn dashed), or null.</summary>
    public List<(double X, double Y)>? PlannedPath { get => _plannedPath; set { _plannedPath = value; Invalidate(); } }
    public int ShapeCount => _shapePoints.Count;
    public int ObjectCount => _shapeKinds.Values.Count(k => k == 1);

    public MapView()
    {
        SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.UserPaint |
                 ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw |
                 ControlStyles.Selectable, true);
        BackColor = Color.White;
        TabStop = true;
        _refresh.Tick += (_, _) => { if (_heatDirty) Invalidate(); };
        _refresh.Start();
    }

    // ---------------------------------------------------------------- data

    public void AddSample(Telemetry t)
    {
        Grow(t.X, t.Y);
        _samples.Add(t);

        if (t.Rssi is double r)
        {
            var key = ((int)Math.Floor(t.X / BinMm), (int)Math.Floor(t.Y / BinMm));
            _bins.TryGetValue(key, out var b);
            _bins[key] = (b.Sum + r, b.Count + 1);
            _heatDirty = true;
        }

        if (_autoFit) FitInternal();
        Invalidate();
    }

    /// <summary>A Wi-Fi reading at the place it was taken: goes into the heat map.</summary>
    public void AddReading(WifiReading r)
    {
        _readings.Add(r);
        Grow(r.X, r.Y);
        var key = ((int)Math.Floor(r.X / BinMm), (int)Math.Floor(r.Y / BinMm));
        _bins.TryGetValue(key, out var b);
        _bins[key] = (b.Sum + r.Rssi, b.Count + 1);
        _heatDirty = true;
        if (_autoFit) FitInternal();
        Invalidate();
    }

    public void AddObstacle(ObstacleEvent o)
    {
        _obstacles.Add(o);
        if (o.ShapeId > 0)
        {
            if (!_shapePoints.TryGetValue(o.ShapeId, out var list))
                _shapePoints[o.ShapeId] = list = new List<ObstacleEvent>();
            list.Add(o);
        }
        Grow(o.X, o.Y);
        if (_autoFit) FitInternal();
        Invalidate();
    }

    public void FinishShape(ShapeEvent e)
    {
        _shapeKinds[e.ShapeId] = e.Kind;
        Invalidate();
    }

    public void Clear()
    {
        _samples.Clear();
        _readings.Clear();
        _obstacles.Clear();
        _shapePoints.Clear();
        _shapeKinds.Clear();
        _corners.Clear();
        _roomShapeId = null;
        _noGo?.Dispose();
        _noGo = null;
        _coverage?.Dispose();
        _coverage = null;
        _plannedPath = null;
        _target = null;
        _hasBounds = false;
        _bins.Clear();
        _heat?.Dispose();
        _heat = null;
        _heatDirty = false;
        _autoFit = true;
        _scale = 0.15;
        _centerX = _centerY = 0;
        Invalidate();
    }

    /// <summary>
    /// Clears the robot's path and the heat map but keeps the room and obstacle outlines
    /// (used before a Wi-Fi survey).
    /// </summary>
    public void ClearSurveyData()
    {
        _samples.Clear();
        _readings.Clear();
        _bins.Clear();
        _heat?.Dispose();
        _heat = null;
        _heatDirty = false;
        _hasBounds = false;
        foreach (var o in _obstacles) Grow(o.X, o.Y);
        Invalidate();
    }

    public void FitToData()
    {
        _autoFit = true;
        FitInternal();
        Invalidate();
    }

    private void FitInternal()
    {
        if (!_hasBounds || Width < 50 || Height < 50) return;
        const double margin = 500, minSpan = 2000;
        double minX = _minX - margin, maxX = _maxX + margin;
        double minY = _minY - margin, maxY = _maxY + margin;
        if (maxX - minX < minSpan) { double c = (minX + maxX) / 2; minX = c - minSpan / 2; maxX = c + minSpan / 2; }
        if (maxY - minY < minSpan) { double c = (minY + maxY) / 2; minY = c - minSpan / 2; maxY = c + minSpan / 2; }

        _centerX = (minX + maxX) / 2;
        _centerY = (minY + maxY) / 2;
        _scale = Math.Clamp(Math.Min(Width / (maxX - minX), Height / (maxY - minY)), 0.005, 5);
    }

    // ---------------------------------------------------------------- coordinates

    private PointF W2S(double x, double y) =>
        new((float)(Width / 2.0 + (x - _centerX) * _scale),
            (float)(Height / 2.0 - (y - _centerY) * _scale));

    private (double X, double Y) S2W(double sx, double sy) =>
        (_centerX + (sx - Width / 2.0) / _scale,
         _centerY - (sy - Height / 2.0) / _scale);

    // ---------------------------------------------------------------- painting

    protected override void OnPaint(PaintEventArgs e)
    {
        var g = e.Graphics;
        g.Clear(BackColor);

        if (_heatDirty && (DateTime.Now - _lastHeatBuild).TotalMilliseconds > 250)
            RebuildHeat();

        DrawGrid(g);
        if (Features.HeatMap && _showHeat) DrawHeat(g);

        if (_coverage != null)
        {
            var ctl = W2S(_covX0, _covYTop);
            var cdest = new RectangleF(ctl.X, ctl.Y, (float)(_coverage.Width * _covCell * _scale),
                                       (float)(_coverage.Height * _covCell * _scale));
            g.InterpolationMode = InterpolationMode.NearestNeighbor;
            g.PixelOffsetMode = PixelOffsetMode.Half;
            g.DrawImage(_coverage, cdest);
            g.PixelOffsetMode = PixelOffsetMode.Default;
        }

        DrawWorldCoverage(g);

        if (_noGo != null)
        {
            var tl = W2S(_noGoX0, _noGoYTop);
            var dest = new RectangleF(tl.X, tl.Y, (float)(_noGo.Width * _noGoCell * _scale),
                                      (float)(_noGo.Height * _noGoCell * _scale));
            g.InterpolationMode = InterpolationMode.NearestNeighbor;
            g.PixelOffsetMode = PixelOffsetMode.Half;
            g.DrawImage(_noGo, dest);
            g.PixelOffsetMode = PixelOffsetMode.Default;
        }

        g.SmoothingMode = SmoothingMode.AntiAlias;
        if (Features.PathLayer && _showPath) DrawPath(g);
        if (_world?.Room == null) { DrawObstacles(g); DrawCorners(g); }   // raw hits until a room is mapped
        DrawWorld(g);
        DrawPlannedPath(g);
        DrawHomeAndTarget(g);
        DrawRobot(g);
        DrawScaleBar(g);
        DrawLegend(g);
        DrawMapKey(g);

        if (_samples.Count == 0)
        {
            using var f = new Font(Font.FontFamily, 12);
            const string msg = "Waiting for data... choose a source and press Connect";
            var size = g.MeasureString(msg, f);
            g.DrawString(msg, f, Brushes.Gray, (Width - size.Width) / 2, (Height - size.Height) / 2);
        }
    }

    private void DrawGrid(Graphics g)
    {
        var (x0, y1) = S2W(0, 0);
        var (x1, y0) = S2W(Width, Height);

        double minor = _scale * 100 >= 8 ? 100 : _scale * 500 >= 8 ? 500 : 1000;
        using var minorPen = new Pen(Color.FromArgb(236, 236, 236));
        using var majorPen = new Pen(Color.FromArgb(195, 195, 195));
        using var axisPen = new Pen(Color.FromArgb(150, 150, 150), 1.5f);
        using var font = new Font("Segoe UI", 8);

        for (double x = Math.Floor(x0 / minor) * minor; x <= x1; x += minor)
        {
            bool major = IsMultipleOf(x, 1000);
            float sx = W2S(x, 0).X;
            g.DrawLine(Math.Abs(x) < 1e-6 ? axisPen : major ? majorPen : minorPen, sx, 0, sx, Height);
            if (major) g.DrawString($"{x / 1000:0} m", font, Brushes.DimGray, sx + 2, Height - 16);
        }
        for (double y = Math.Floor(y0 / minor) * minor; y <= y1; y += minor)
        {
            bool major = IsMultipleOf(y, 1000);
            float sy = W2S(0, y).Y;
            g.DrawLine(Math.Abs(y) < 1e-6 ? axisPen : major ? majorPen : minorPen, 0, sy, Width, sy);
            if (major) g.DrawString($"{y / 1000:0} m", font, Brushes.DimGray, 2, sy - 14);
        }
    }

    private static bool IsMultipleOf(double v, double step) =>
        Math.Abs(v / step - Math.Round(v / step)) < 1e-6;

    // ---------------------------------------------------------------- mouse

    protected override void OnMouseEnter(EventArgs e)
    {
        base.OnMouseEnter(e);
        Focus(); // so the scroll wheel works
    }

    protected override void OnMouseWheel(MouseEventArgs e)
    {
        base.OnMouseWheel(e);
        var (wx, wy) = S2W(e.X, e.Y);
        _scale = Math.Clamp(_scale * (e.Delta > 0 ? 1.2 : 1 / 1.2), 0.005, 5);
        _centerX = wx - (e.X - Width / 2.0) / _scale;   // keep the point under the cursor fixed
        _centerY = wy + (e.Y - Height / 2.0) / _scale;
        _autoFit = false;
        Invalidate();
    }

    /// <summary>A plain left click (not a drag) on the map, in map coordinates (mm).</summary>
    public event Action<(double X, double Y)>? MapClicked;
    private Point _clickStart;

    protected override void OnMouseDown(MouseEventArgs e)
    {
        base.OnMouseDown(e);
        _clickStart = e.Location;
        if (e.Button != MouseButtons.Left) return;
        _dragging = true;
        _dragStart = e.Location;
        _dragCX = _centerX;
        _dragCY = _centerY;
    }

    protected override void OnMouseMove(MouseEventArgs e)
    {
        base.OnMouseMove(e);
        if (!_dragging) return;
        _centerX = _dragCX - (e.X - _dragStart.X) / _scale;
        _centerY = _dragCY + (e.Y - _dragStart.Y) / _scale;
        _autoFit = false;
        Invalidate();
    }

    protected override void OnMouseUp(MouseEventArgs e)
    {
        base.OnMouseUp(e);
        if (e.Button == MouseButtons.Left && Math.Abs(e.X - _clickStart.X) + Math.Abs(e.Y - _clickStart.Y) < 5)
            MapClicked?.Invoke(S2W(e.X, e.Y));
        _dragging = false;
    }

    protected override void OnResize(EventArgs e)
    {
        base.OnResize(e);
        if (_autoFit) FitInternal();
    }

    protected override void Dispose(bool disposing)
    {
        if (disposing)
        {
            _refresh.Dispose();
            _heat?.Dispose();
        }
        base.Dispose(disposing);
    }
}

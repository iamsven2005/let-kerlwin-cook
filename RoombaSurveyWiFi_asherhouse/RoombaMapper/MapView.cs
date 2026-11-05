using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;

namespace RoombaMapper;

/// <summary>
/// Draws a floor-plan style grid, the robot's path, the robot itself, and a Wi-Fi
/// heat map built from signal readings. Scroll to zoom, drag to pan.
/// </summary>
public sealed class MapView : Control
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

    // ---------------------------------------------------------------- heat map

    private void RebuildHeat()
    {
        _heatDirty = false;
        _lastHeatBuild = DateTime.Now;
        _heat?.Dispose();
        _heat = null;
        if (_bins.Count == 0) return;

        var pts = _bins.Select(kv => (
            X: (kv.Key.Item1 + 0.5) * BinMm,
            Y: (kv.Key.Item2 + 0.5) * BinMm,
            V: kv.Value.Sum / kv.Value.Count)).ToArray();

        double minX = pts.Min(p => p.X) - HeatRadiusMm, maxX = pts.Max(p => p.X) + HeatRadiusMm;
        double minY = pts.Min(p => p.Y) - HeatRadiusMm, maxY = pts.Max(p => p.Y) + HeatRadiusMm;
        int w = Math.Max(1, (int)Math.Ceiling((maxX - minX) / CellMm));
        int h = Math.Max(1, (int)Math.Ceiling((maxY - minY) / CellMm));

        var pixels = new int[w * h];
        int empty = Color.FromArgb(0, 255, 255, 255).ToArgb(); // transparent white avoids dark edges
        double r2 = HeatRadiusMm * HeatRadiusMm;

        for (int j = 0; j < h; j++)
        {
            double wy = maxY - (j + 0.5) * CellMm; // row 0 is the top of the map
            for (int i = 0; i < w; i++)
            {
                double wx = minX + (i + 0.5) * CellMm;
                double wSum = 0, vSum = 0, nearest = double.MaxValue;

                foreach (var p in pts)
                {
                    double dx = p.X - wx, dy = p.Y - wy, d2 = dx * dx + dy * dy;
                    if (d2 > r2) continue;
                    if (d2 < nearest) nearest = d2;
                    double weight = 1.0 / Math.Max(d2, 2500); // inverse-distance weighting
                    wSum += weight;
                    vSum += weight * p.V;
                }

                if (wSum == 0) { pixels[j * w + i] = empty; continue; }

                double fade = Math.Clamp((1 - Math.Sqrt(nearest) / HeatRadiusMm) * 1.6, 0, 1);
                pixels[j * w + i] = ColorFor(vSum / wSum, (int)(190 * fade)).ToArgb();
            }
        }

        var bmp = new Bitmap(w, h, PixelFormat.Format32bppArgb);
        var data = bmp.LockBits(new Rectangle(0, 0, w, h), ImageLockMode.WriteOnly, PixelFormat.Format32bppArgb);
        Marshal.Copy(pixels, 0, data.Scan0, pixels.Length);
        bmp.UnlockBits(data);

        _heat = bmp;
        _heatMinX = minX;
        _heatMaxY = maxY;
        _heatW = w;
        _heatH = h;
    }

    public Color ColorFor(double rssi, int alpha = 255)
    {
        double t = Math.Clamp((rssi - RssiWeak) / (RssiStrong - RssiWeak), 0, 1);
        return FromHsv(120 * t, 0.85, 0.95, alpha); // red (weak) -> yellow -> green (strong)
    }

    private static Color FromHsv(double hue, double sat, double val, int alpha)
    {
        double c = val * sat, x = c * (1 - Math.Abs(hue / 60 % 2 - 1)), m = val - c;
        (double r, double g, double b) = hue switch
        {
            < 60 => (c, x, 0.0),
            < 120 => (x, c, 0.0),
            _ => (0.0, c, x)
        };
        return Color.FromArgb(alpha, (int)((r + m) * 255), (int)((g + m) * 255), (int)((b + m) * 255));
    }

    // ---------------------------------------------------------------- painting

    protected override void OnPaint(PaintEventArgs e)
    {
        var g = e.Graphics;
        g.Clear(BackColor);

        if (_heatDirty && (DateTime.Now - _lastHeatBuild).TotalMilliseconds > 250)
            RebuildHeat();

        DrawGrid(g);
        if (_showHeat) DrawHeat(g);

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
        if (_showPath) DrawPath(g);
        DrawObstacles(g);
        DrawCorners(g);
        DrawPlannedPath(g);
        DrawHomeAndTarget(g);
        DrawRobot(g);
        DrawScaleBar(g);
        DrawLegend(g);

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

    private void DrawHeat(Graphics g)
    {
        if (_heat == null) return;
        var tl = W2S(_heatMinX, _heatMaxY);
        var dest = new Rectangle(
            (int)Math.Round(tl.X), (int)Math.Round(tl.Y),
            (int)Math.Round(_heatW * CellMm * _scale), (int)Math.Round(_heatH * CellMm * _scale));

        // Keep the heat inside the room walls once the room has been mapped
        using var roomClip = new GraphicsPath();
        if (_roomShapeId is int rid && _corners.TryGetValue(rid, out var room) && room.Count >= 3)
        {
            roomClip.AddPolygon(room.Select(p => W2S(p.X, p.Y)).ToArray());
            g.SetClip(roomClip);
        }

        g.InterpolationMode = InterpolationMode.HighQualityBilinear;
        g.PixelOffsetMode = PixelOffsetMode.Half;
        using var attrs = new ImageAttributes();
        attrs.SetWrapMode(WrapMode.TileFlipXY); // avoids smeared edges
        g.DrawImage(_heat, dest, 0, 0, _heatW, _heatH, GraphicsUnit.Pixel, attrs);
        g.PixelOffsetMode = PixelOffsetMode.Default;
        g.ResetClip();
    }

    private void DrawPath(Graphics g)
    {
        if (_samples.Count < 2) return;
        var pts = new PointF[_samples.Count];
        for (int i = 0; i < pts.Length; i++) pts[i] = W2S(_samples[i].X, _samples[i].Y);

        using var pen = new Pen(Color.FromArgb(200, 30, 60, 140), 2f) { LineJoin = LineJoin.Round };
        g.DrawLines(pen, pts);

        var start = pts[0];
        g.FillEllipse(Brushes.SeaGreen, start.X - 5, start.Y - 5, 10, 10);
        g.DrawString("First reading", Font, Brushes.SeaGreen, start.X + 7, start.Y - 7);
    }

    private void DrawObstacles(Graphics g)
    {
        if (_obstacles.Count == 0) return;

        // Traced outlines
        using var objFill = new SolidBrush(Color.FromArgb(110, 120, 120, 130));
        using var outlinePen = new Pen(Color.FromArgb(230, 40, 40, 50), 3f) { LineJoin = LineJoin.Round };
        using var wallPen = new Pen(Color.FromArgb(240, 20, 20, 20), 5f) { LineJoin = LineJoin.Round };
        using var openPen = new Pen(Color.FromArgb(220, 200, 60, 20), 3f) { LineJoin = LineJoin.Round };

        foreach (var (id, pts) in _shapePoints)
        {
            _shapeKinds.TryGetValue(id, out var kind);
            bool finished = _shapeKinds.ContainsKey(id);
            var screen = pts.Select(p => W2S(p.X, p.Y)).ToArray();

            if (kind == 1 && screen.Length >= 3)
            {
                // A finished object: fill its outline
                g.FillPolygon(objFill, screen);
                g.DrawPolygon(outlinePen, screen);
                continue;
            }

            kind = KindOf(id);
            var pen = kind == 2 ? wallPen : (finished ? openPen : outlinePen);
            if (kind == 2 && _corners.ContainsKey(id))
            {
                // Room walls: the corner outline is drawn instead; just mark the edge points
                foreach (var sp in screen) g.FillEllipse(Brushes.Black, sp.X - 3f, sp.Y - 3f, 6, 6);
                continue;
            }
            // Join neighbouring points, but leave gaps where the robot lost the edge
            for (int i = 1; i < pts.Count; i++)
            {
                double dx = pts[i].X - pts[i - 1].X, dy = pts[i].Y - pts[i - 1].Y;
                if (dx * dx + dy * dy <= OutlineGapMm * OutlineGapMm)
                    g.DrawLine(pen, screen[i - 1], screen[i]);
            }
            if (kind == 2 && pts.Count >= 3) g.DrawLine(pen, screen[^1], screen[0]);
            foreach (var sp in screen) g.FillEllipse(Brushes.Black, sp.X - 1.5f, sp.Y - 1.5f, 3, 3);
        }

        // Single hits that were not traced
        float r = (float)Math.Max(5, 40 * _scale);
        using var xPen = new Pen(Color.FromArgb(220, 200, 20, 20), 2.5f);
        foreach (var o in _obstacles)
        {
            if (o.ShapeId != 0) continue;
            var c = W2S(o.X, o.Y);
            g.DrawLine(xPen, c.X - r, c.Y - r, c.X + r, c.Y + r);
            g.DrawLine(xPen, c.X - r, c.Y + r, c.X + r, c.Y - r);
        }
    }

    private void DrawCorners(Graphics g)
    {
        if (_corners.Count == 0) return;
        using var font = new Font("Segoe UI", 8, FontStyle.Bold);
        using var dashPen = new Pen(Color.FromArgb(160, 30, 90, 160), 1.5f) { DashStyle = DashStyle.Dash };
        using var wallPen = new Pen(Color.FromArgb(230, 25, 25, 35), 4f) { LineJoin = LineJoin.Round };
        foreach (var (id, pts) in _corners)
        {
            if (pts.Count < 2) continue;
            int kind = KindOf(id);
            var pen = kind == 2 ? wallPen : dashPen;   // room walls solid, objects dashed
            for (int i = 0; i < pts.Count; i++)
            {
                var a = pts[i];
                var b = pts[(i + 1) % pts.Count];
                var sa = W2S(a.X, a.Y);
                var sb = W2S(b.X, b.Y);
                g.DrawLine(pen, sa, sb);
                double len = Math.Sqrt((b.X - a.X) * (b.X - a.X) + (b.Y - a.Y) * (b.Y - a.Y));
                if (len > 300)
                    g.DrawString($"{len / 1000:0.00} m", font, Brushes.SteelBlue, (sa.X + sb.X) / 2 + 3, (sa.Y + sb.Y) / 2 + 3);
                g.FillRectangle(Brushes.SteelBlue, sa.X - 3, sa.Y - 3, 6, 6);
            }
        }
    }

    private void DrawPlannedPath(Graphics g)
    {
        if (_plannedPath is not { Count: >= 2 } path) return;
        var pts = path.Select(p => W2S(p.X, p.Y)).ToArray();
        using var pen = new Pen(Color.FromArgb(200, 140, 40, 200), 2.5f) { DashStyle = DashStyle.Dash };
        g.DrawLines(pen, pts);
        foreach (var sp in pts) g.FillEllipse(Brushes.MediumPurple, sp.X - 2.5f, sp.Y - 2.5f, 5, 5);
    }

    private void DrawHomeAndTarget(Graphics g)
    {
        // The robot's starting point (0,0) - where "Return to start" goes
        var h = W2S(0, 0);
        using (var homePen = new Pen(Color.SeaGreen, 2f))
        {
            g.DrawRectangle(homePen, h.X - 7, h.Y - 7, 14, 14);
            g.DrawLine(homePen, h.X, h.Y, h.X + 14, h.Y);   // starting direction (+X)
        }
        g.DrawString("Home", Font, Brushes.SeaGreen, h.X - 14, h.Y + 9);

        if (_target is { } tg)
        {
            var t = W2S(tg.X, tg.Y);
            using var pen = new Pen(Color.DarkOrange, 2f);
            g.DrawEllipse(pen, t.X - 10, t.Y - 10, 20, 20);
            g.DrawEllipse(pen, t.X - 4, t.Y - 4, 8, 8);
            g.DrawLine(pen, t.X - 14, t.Y, t.X + 14, t.Y);
            g.DrawLine(pen, t.X, t.Y - 14, t.X, t.Y + 14);
        }
    }

    private void DrawRobot(Graphics g)
    {
        if (_samples.Count == 0) return;
        var t = _samples[^1];
        var c = W2S(t.X, t.Y);
        float r = (float)Math.Max(6, RobotRadiusMm * _scale);
        double a = t.HeadingDeg * Math.PI / 180;

        bool anyIr = t.Ir == true || t.IrLeft == true || t.IrRight == true;
        using var fill = new SolidBrush(anyIr ? Color.FromArgb(230, 200, 30, 30) : Color.FromArgb(220, 60, 60, 60));
        g.FillEllipse(fill, c.X - r, c.Y - r, 2 * r, 2 * r);
        using var pen = new Pen(Color.White, 2f);
        g.DrawLine(pen, c.X, c.Y, c.X + (float)(Math.Cos(a) * r), c.Y - (float)(Math.Sin(a) * r));
    }

    private void DrawScaleBar(Graphics g)
    {
        double lengthMm = _scale * 1000 > 40 ? 1000 : _scale * 5000 > 40 ? 5000 : 10000;
        float px = (float)(lengthMm * _scale);
        float x = 15, y = Height - 35;
        using var pen = new Pen(Color.Black, 2f);
        g.DrawLine(pen, x, y, x + px, y);
        g.DrawLine(pen, x, y - 5, x, y + 5);
        g.DrawLine(pen, x + px, y - 5, x + px, y + 5);
        g.DrawString($"{lengthMm / 1000:0} m", Font, Brushes.Black, x + px / 2 - 10, y - 18);
    }

    private void DrawLegend(Graphics g)
    {
        const int w = 180, h = 12;
        int x = Width - w - 20, y = 28;
        using (var bg = new SolidBrush(Color.FromArgb(220, 255, 255, 255)))
            g.FillRectangle(bg, x - 8, y - 22, w + 16, h + 62);

        g.DrawString("Wi-Fi signal (dBm)", Font, Brushes.Black, x, y - 18);
        for (int i = 0; i < w; i++)
        {
            double rssi = RssiWeak + (RssiStrong - RssiWeak) * i / (w - 1);
            using var p = new Pen(ColorFor(rssi));
            g.DrawLine(p, x + i, y, x + i, y + h);
        }
        g.DrawRectangle(Pens.Gray, x, y, w, h);
        g.DrawString($"{RssiWeak:0} weak", Font, Brushes.Black, x - 2, y + h + 2);
        var strong = $"{RssiStrong:0} strong";
        var size = g.MeasureString(strong, Font);
        g.DrawString(strong, Font, Brushes.Black, x + w - size.Width + 2, y + h + 2);

        float ox = x + 5, oy = y + h + 27;
        using (var fillKey = new SolidBrush(Color.FromArgb(110, 120, 120, 130)))
            g.FillRectangle(fillKey, ox - 5, oy - 5, 10, 10);
        g.DrawRectangle(Pens.Black, ox - 5, oy - 5, 10, 10);
        g.DrawString($"Obstacles traced ({ObjectCount})", Font, Brushes.Black, ox + 10, oy - 8);
    }

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

    protected override void OnMouseDown(MouseEventArgs e)
    {
        base.OnMouseDown(e);
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

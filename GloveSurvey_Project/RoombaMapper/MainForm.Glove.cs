using System.Globalization;
using RoombaMapper.Core;

namespace RoombaMapper;

/// <summary>
/// The survey glove (GloveSurvey.ino), worn on the right hand. The PERSON moves, so there
/// are no mapping phases to run: the person walks, and the app records.
///
///   Position = steps x step length along the person's heading (counted on the glove).
///   Heading  = the PERSON's facing, set by turn buttons (glove or here) and corrected with
///              "I'm here" clicks. Never the hand's direction: the accelerometer can't
///              measure turning, and the hand doesn't point where the person faces.
///   IR hits  = plotted by the glove only in a calibrated hand pose, in the direction the
///              SENSOR pointed (HANG: to the person's right; POINT: straight ahead).
///
///   Room walk:     walk round the room keeping the walls on your RIGHT, arm hanging, so the
///                  glove's IR sees the wall; press a turn button at every corner.
///   Obstacle walk: walk round the obstacle keeping it on your RIGHT the same way.
///   Survey guide:  walk the dashed lanes; covered areas turn green; Wi-Fi readings build the
///                  heat map exactly as with the robot.
/// </summary>
public sealed partial class MainForm
{
    private readonly FlowLayoutPanel _gloveRow = new()
    {
        Dock = DockStyle.Top, AutoSize = true, Padding = new Padding(6, 0, 6, 6), WrapContents = true,
        BackColor = Color.FromArgb(236, 247, 240)
    };
    private readonly Label _gloveLbl = new() { AutoSize = true, Margin = new Padding(10, 7, 3, 3), ForeColor = Color.DarkGreen };
    private readonly NumericUpDown _calDist = new() { Minimum = 3, Maximum = 50, DecimalPlaces = 1, Increment = 0.5m, Value = 5, Width = 50 };

    private enum GloveWalk { None, Room, Obstacle }
    private GloveWalk _walk = GloveWalk.None;
    private readonly List<Pose> _walkPath = new();
    private readonly List<TraceHit> _walkHits = new();
    private int _poseClick;                         // "I'm here": 1 = waiting for position, 2 = for facing
    private (double X, double Y) _poseClickAt;
    private bool _guiding;
    private double _guideAnchor;
    private bool _guideVertical;
    private DateTime _lastGuide = DateTime.MinValue;

    private void BuildGloveRow()
    {
        Button B(string text, Action click)
        {
            var b = CtlButton(text);
            b.Click += (_, _) => click();
            return b;
        }
        var tip = new ToolTip();
        var here = B("📍 I'm here", () =>
        {
            _poseClick = 1;
            GloveStatus("Click on the map where you are standing...");
        });
        tip.SetToolTip(here, "Correct your position: click where you stand, then click a point in the direction you face");

        _gloveRow.Controls.AddRange(new Control[]
        {
            Caption("Glove - I turned:"),
            B("↶ Left 90", () => SendCommand("TURN,90", "Turn left")),
            B("↷ Right 90", () => SendCommand("TURN,-90", "Turn right")),
            B("↻ Around", () => SendCommand("TURN,180", "Turn around")),
            here,
            Caption("Step length - walk"), _calDist, Caption("m:"),
            B("Start", () => { SendCommand("CALSTEP_START", "Step calibration"); GloveStatus($"Walk exactly {_calDist.Value} m in a straight line, then press Finish."); }),
            B("Finish", () => SendCommand(string.Create(CultureInfo.InvariantCulture, $"CALSTEP_END,{_calDist.Value * 1000:0}"), "Step calibration")),
            Caption("Hand pose:"),
            B("Hanging", () => { SendCommand("POSECAL,HANG", "Hand pose"); GloveStatus("Hanging pose stored (arm relaxed at your side, sensor facing right)."); }),
            B("Pointing", () => { SendCommand("POSECAL,POINT", "Hand pose"); GloveStatus("Pointing pose stored (hand raised, sensor facing forward)."); }),
            Caption("Room walk:"), B("Start", () => StartWalk(GloveWalk.Room)), B("Finish", FinishRoomWalk),
            Caption("Obstacle walk:"), B("Start", () => StartWalk(GloveWalk.Obstacle)), B("Finish", FinishObstacleWalk),
            Caption("Survey guide:"), B("Start", StartGuide), B("Finish", FinishGuide),
            _gloveLbl,
        });
        _map.MapClicked += OnMapClickedForPose;
        _gloveLbl.Text = "Glove: connect with Wi-Fi (UDP), then calibrate hand poses and step length.";
    }

    private void GloveStatus(string text)
    {
        _gloveLbl.Text = text;
        SetStatus(text);
    }

    private RobotConfig GloveCfg() => new()
    {
        // Used by the room builder and planner for a PERSON with the glove:
        // the hanging hand (where the IR is) is ~0.25 m from the body centre
        LengthMm = 500, WidthMm = 450, RangeMm = (double)_rangeBox.Value,
        SurveyLaneMm = (double)_laneSpacing.Value * 10,
    };

    // ---------------------------------------------------------------- "I'm here"

    private void OnMapClickedForPose((double X, double Y) p)
    {
        if (_poseClick == 1)
        {
            _poseClickAt = p;
            _poseClick = 2;
            _map.Target = p;
            GloveStatus("Now click a point in the direction you are FACING (your body, not your hand)...");
        }
        else if (_poseClick == 2)
        {
            _poseClick = 0;
            double h = Geometry.BearingDeg(_poseClickAt, p);
            SendCommand(string.Create(CultureInfo.InvariantCulture, $"SETPOSE,{_poseClickAt.X:0},{_poseClickAt.Y:0},{h:0}"), "I'm here");
            _map.Target = null;
            GloveStatus($"Position set: {_poseClickAt.X / 1000:0.00}, {_poseClickAt.Y / 1000:0.00} m, facing {h:0}°.");
        }
    }

    // ---------------------------------------------------------------- walks

    private void StartWalk(GloveWalk w)
    {
        if (w == GloveWalk.Room && _world.Room != null &&
            MessageBox.Show(this, "Map the room again? The current map will be replaced.", "Room walk",
                            MessageBoxButtons.OKCancel) != DialogResult.OK) return;
        if (w == GloveWalk.Obstacle && _world.Room == null) { GloveStatus("Walk the room first."); return; }
        if (w == GloveWalk.Room)
        {
            ClearAll();
            _world = new WorldMap();
            SendCommand("RESET", "Start point");          // here = (0,0), facing 0
            MapChangedNow();
        }
        _walk = w;
        _walkPath.Clear();
        _walkHits.Clear();
        GloveStatus(w == GloveWalk.Room
            ? "Room walk: keep the walls on your RIGHT, arm hanging (sensor facing the wall). Press a turn button at every corner. Back at the start: Finish."
            : "Obstacle walk: walk all the way round it keeping it on your RIGHT, arm hanging. Back where you started: Finish.");
    }

    private void GloveOnTelemetry(Telemetry t)
    {
        if (t.Steps == null) return;                       // not the glove
        if (_walk != GloveWalk.None)
        {
            var p = new Pose(t.X, t.Y, t.HeadingDeg);
            if (_walkPath.Count == 0 || Geometry.Dist(_walkPath[^1].Pos, p.Pos) > 20 ||
                Math.Abs(Geometry.Wrap180(_walkPath[^1].HeadingDeg - p.HeadingDeg)) > 1)
                _walkPath.Add(p);
        }
        if (_guiding) GuideStep(t);
        if (t.Sensors is "OTHER" && t.Ir == true)
            _gloveLbl.Text = "IR sees something, but the hand isn't in a calibrated pose: not plotted.";
    }

    private void GloveOnHit(ObstacleEvent o)
    {
        if (_walk == GloveWalk.None || o.FacingDeg is not double f) return;
        if (_walkPath.Count == 0) _walkPath.Add(_link.Pose);
        _walkHits.Add(new TraceHit((o.X, o.Y), f, _walkPath.Count - 1, false, false));
    }

    private void FinishRoomWalk()
    {
        if (_walk != GloveWalk.Room) { GloveStatus("Start a room walk first."); return; }
        _walk = GloveWalk.None;
        if (_walkHits.Count < 3 || _walkPath.Count < 4)
        {
            GloveStatus($"Not enough wall readings ({_walkHits.Count}). Keep your arm hanging next to the walls and try again.");
            return;
        }
        // The walk is a trace: the same post-processing as the robot's room trace
        var t = new TraceResult { End = TraceEnd.Closed, Inside = true, Start = _walkPath[0] };
        double dist = 0;
        for (int i = 0; i < _walkPath.Count; i++)
        {
            if (i > 0) dist += Geometry.Dist(_walkPath[i - 1].Pos, _walkPath[i].Pos);
            t.Path.Add(_walkPath[i]);
            t.PathDist.Add(dist);
            t.PathTheta.Add(0);
        }
        t.DistanceMm = dist;
        t.Hits.AddRange(_walkHits);
        _world.ClearAll();
        var built = RoomBuilder.Build(t, GloveCfg(), _world);
        _world.Room = built.Room;
        _world.Room.Closed = Geometry.Dist(_walkPath[0].Pos, _walkPath[^1].Pos) < 1000;
        _world.TracePath.AddRange(_walkPath.Select(p => p.Pos));
        foreach (var n in built.Notes) AppendLog("[glove] " + n);
        MapChangedNow();
        _map.FitToData();
        UpdateMissionButtons();
        var poly = _world.Room.Polygon;
        GloveStatus($"Room: {poly.Count} corners, {(poly.Max(p => p.X) - poly.Min(p => p.X)) / 1000:0.0} x " +
                    $"{(poly.Max(p => p.Y) - poly.Min(p => p.Y)) / 1000:0.0} m" +
                    (_world.Room.Closed ? "." : " - you didn't finish near the start: outline marked INCOMPLETE."));
    }

    private void FinishObstacleWalk()
    {
        if (_walk != GloveWalk.Obstacle) { GloveStatus("Start an obstacle walk first."); return; }
        _walk = GloveWalk.None;
        var pts = _walkHits.Select(h => h.P).ToList();
        if (pts.Count < 2) { GloveStatus("No readings of the obstacle: keep your hanging hand next to it."); return; }
        var (l, s) = Geometry.MinAreaRect(pts);
        var kind = l / Math.Max(s, 1) >= 2.5 ? ObstacleKind.Partition : ObstacleKind.Pillar;
        var o = _world.AddObstacle(kind, Footprints.FromPoints(pts), pts, "walked round with the glove");
        bool closed = _walkPath.Count > 2 && Geometry.Dist(_walkPath[0].Pos, _walkPath[^1].Pos) < 800;
        o.Unconfirmed = !closed;
        MapChangedNow();
        GloveStatus($"{kind} added ({l / 1000:0.00} x {s / 1000:0.00} m){(closed ? "" : " - not walked all the way round: unconfirmed")}.");
    }

    // ---------------------------------------------------------------- survey guide

    private void StartGuide()
    {
        if (_world.Room == null) { GloveStatus("Walk the room first (or load a map)."); return; }
        _map.ClearSurveyData();
        _withSignal = 0;
        _world.ClearCoverage();
        var poly = _world.Room.Polygon;
        _guideVertical = poly.Max(p => p.Y) - poly.Min(p => p.Y) > poly.Max(p => p.X) - poly.Min(p => p.X);
        var range = _world.BuildPlanner(GloveCfg()).FreeRange(_guideVertical);
        if (range == null) { GloveStatus("No free space to survey inside the room."); return; }
        _guideAnchor = range.Value.VMin;
        _guiding = true;
        GloveStatus("Survey: walk the dashed purple lanes. Covered areas turn green. Finish when done.");
    }

    private void GuideStep(Telemetry t)
    {
        double spacing = (double)_laneSpacing.Value * 10;
        double r = spacing / 2 - 10;
        int cr = (int)Math.Ceiling(r / WorldMap.CoverCellMm);
        int cx = (int)Math.Floor(t.X / WorldMap.CoverCellMm), cy = (int)Math.Floor(t.Y / WorldMap.CoverCellMm);
        for (int dy = -cr; dy <= cr; dy++)
            for (int dx = -cr; dx <= cr; dx++)
            {
                double px = (cx + dx + 0.5) * WorldMap.CoverCellMm, py = (cy + dy + 0.5) * WorldMap.CoverCellMm;
                if ((px - t.X) * (px - t.X) + (py - t.Y) * (py - t.Y) <= r * r) _world.Covered.Add((cx + dx, cy + dy));
            }
        if ((DateTime.Now - _lastGuide).TotalSeconds < 1.5) { _map.Invalidate(); return; }
        _lastGuide = DateTime.Now;

        // What's left, and the order to walk it in (nearest first)
        var planner = _world.BuildPlanner(GloveCfg());
        bool Done(double x, double y) =>
            _world.Covered.Contains(((int)Math.Floor(x / WorldMap.CoverCellMm), (int)Math.Floor(y / WorldMap.CoverCellMm)));
        var segs = planner.LaneSegments(spacing, _guideAnchor, _guideVertical, Done, 300);
        (double X, double Y) P(double v, double u) => _guideVertical ? (v, u) : (u, v);
        var path = new List<(double X, double Y)> { (t.X, t.Y) };
        var left = segs.ToList();
        var at = (t.X, t.Y);
        while (left.Count > 0 && path.Count < 120)
        {
            var next = left.OrderBy(s => Math.Min(Geometry.Dist(at, P(s.V, s.A)), Geometry.Dist(at, P(s.V, s.B)))).First();
            bool fromA = Geometry.Dist(at, P(next.V, next.A)) <= Geometry.Dist(at, P(next.V, next.B));
            var a = P(next.V, fromA ? next.A : next.B);
            var b = P(next.V, fromA ? next.B : next.A);
            path.Add(a); path.Add(b);
            at = b;
            left.Remove(next);
        }
        _map.PlannedPath = path;
        _world.Remaining.Clear();
        foreach (var s in segs)
            for (double u = s.A; u <= s.B; u += WorldMap.CoverCellMm)
            {
                var q = P(s.V, u);
                _world.Remaining.Add(((int)Math.Floor(q.X / WorldMap.CoverCellMm), (int)Math.Floor(q.Y / WorldMap.CoverCellMm)));
            }
        double leftM = segs.Sum(s => s.Length) / 1000;
        _gloveLbl.Text = segs.Count == 0 ? "Survey: everything is covered - press Finish."
                                         : $"Survey: {segs.Count} lane pieces ({leftM:0.0} m) left, {_map.Readings.Count} readings.";
        _map.Invalidate();
    }

    private void FinishGuide()
    {
        if (!_guiding) return;
        _guiding = false;
        _map.PlannedPath = null;
        GloveStatus($"Survey finished: {_map.Readings.Count} readings.");
        if (MessageBox.Show(this, $"Survey finished with {_map.Readings.Count} readings. Export the heat map now?",
                            "Survey", MessageBoxButtons.YesNo) == DialogResult.Yes)
            ExportHeatmap();
    }
}

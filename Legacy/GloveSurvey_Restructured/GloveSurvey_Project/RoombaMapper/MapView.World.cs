using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
using RoombaMapper.Core;

namespace RoombaMapper;

/// <summary>MapView (see MapView.cs): mapped world layer - coverage, room, footprints, seeds, map key and legend</summary>
public sealed partial class MapView
{
    // ---------------------------------------------------------------- world drawing

    /// <summary>Covered (green), remaining (blue), skipped (amber), unreachable (grey) cells.</summary>
    private void DrawWorldCoverage(Graphics g)
    {
        if (_world == null) return;
        var sets = new[] { _world.Covered, _world.Remaining, _world.Skipped, _world.Unreachable };
        long key = sets.Aggregate(17L, (k, set) => k * 31 + set.Count);
        if (key != _worldCovKey)
        {
            _worldCovKey = key;
            _worldCov?.Dispose();
            _worldCov = null;
            var all = sets.SelectMany(x => x).ToList();
            if (all.Count > 0)
            {
                int minX = all.Min(k => k.Item1), maxX = all.Max(k => k.Item1);
                int minY = all.Min(k => k.Item2), maxY = all.Max(k => k.Item2);
                int w = maxX - minX + 1, h = maxY - minY + 1;
                if ((long)w * h <= 4_000_000)
                {
                    var px = new int[w * h];
                    var colors = new[]
                    {
                        Color.FromArgb(55, 60, 180, 90), Color.FromArgb(60, 80, 150, 230),
                        Color.FromArgb(80, 230, 160, 30), Color.FromArgb(70, 120, 120, 120),
                    };
                    for (int i = 0; i < sets.Length; i++)
                        foreach (var (x, y) in sets[i]) px[(maxY - y) * w + (x - minX)] = colors[i].ToArgb();
                    _worldCov = new Bitmap(w, h, PixelFormat.Format32bppArgb);
                    var data = _worldCov.LockBits(new Rectangle(0, 0, w, h), ImageLockMode.WriteOnly, PixelFormat.Format32bppArgb);
                    Marshal.Copy(px, 0, data.Scan0, px.Length);
                    _worldCov.UnlockBits(data);
                    _worldCovX0 = minX * WorldMap.CoverCellMm;
                    _worldCovYTop = (maxY + 1) * WorldMap.CoverCellMm;
                }
            }
        }
        if (_worldCov == null) return;
        var tl = W2S(_worldCovX0, _worldCovYTop);
        var dest = new RectangleF(tl.X, tl.Y, (float)(_worldCov.Width * WorldMap.CoverCellMm * _scale),
                                  (float)(_worldCov.Height * WorldMap.CoverCellMm * _scale));
        g.InterpolationMode = InterpolationMode.NearestNeighbor;
        g.PixelOffsetMode = PixelOffsetMode.Half;
        g.DrawImage(_worldCov, dest);
        g.PixelOffsetMode = PixelOffsetMode.Default;
    }

    /// <summary>Room sections (by confidence), obstacles (by class), seeds and the last trace.</summary>
    private void DrawWorld(Graphics g)
    {
        if (_world == null) return;
        using var font = new Font("Segoe UI", 8, FontStyle.Bold);

        // Last trace (faint)
        if (_world.TracePath.Count >= 2)
        {
            using var tracePen = new Pen(Color.FromArgb(90, 120, 120, 120), 1f);
            g.DrawLines(tracePen, _world.TracePath.Select(p => W2S(p.X, p.Y)).ToArray());
        }

        // Obstacles
        foreach (var o in _world.Obstacles)
        {
            if (o.Footprint.Count < 3) continue;
            var pts = o.Polygon.Select(p => W2S(p.X, p.Y)).ToArray();
            var (fill, label) = o.Kind switch
            {
                ObstacleKind.Pillar => (Color.FromArgb(150, 90, 90, 105), "pillar"),
                ObstacleKind.Partition => (Color.FromArgb(150, 110, 85, 60), "partition"),
                ObstacleKind.AttachedPartition => (Color.FromArgb(150, 140, 90, 50), "attached partition"),
                ObstacleKind.WallPillar => (Color.FromArgb(150, 90, 90, 105), "wall pillar"),
                ObstacleKind.Cluster => (Color.FromArgb(100, 140, 90, 200), "cluster " + o.Cluster switch
                {
                    ClusterState.Closed => "(closed)", ClusterState.WallAttached => "(wall-attached)",
                    ClusterState.ObstacleAttached => "(obstacle-attached)", _ => "(unconfirmed)",
                }),
                ObstacleKind.Temporary => (Color.FromArgb(110, 255, 140, 0), "temporary"),
                _ => (Color.Transparent, "unresolved loop"),
            };
            if (fill.A > 0) using (var b = new SolidBrush(fill)) g.FillPolygon(b, pts);
            using var outline = new Pen(Color.FromArgb(220, 40, 40, 50), 1.5f);
            if (o.Unconfirmed || o.Kind is ObstacleKind.Temporary or ObstacleKind.UnresolvedLoop) outline.DashStyle = DashStyle.Dash;
            g.DrawPolygon(outline, pts);
            if (o.InferredEdge) label += ", inferred edge";
            if (o.Unconfirmed) label += " ?";
            var c = (pts.Average(p => p.X), pts.Average(p => p.Y));
            var size = g.MeasureString(label, font);
            g.DrawString(label, font, Brushes.Black, (float)c.Item1 - size.Width / 2, (float)c.Item2 - size.Height / 2);
        }

        // Room: each section in its own style; wall lengths per edge
        if (_world.Room is { Corners.Count: >= 3 } room)
        {
            foreach (var sec in room.Sections)
            {
                var (a, b) = room.Ends(sec);
                using var pen = sec.Kind switch
                {
                    SectionKind.Doorway => new Pen(Color.FromArgb(230, 30, 160, 60), 3f) { DashStyle = DashStyle.Dot },
                    SectionKind.Inferred => new Pen(Color.FromArgb(230, 30, 90, 200), 3.5f) { DashStyle = DashStyle.Dash },
                    SectionKind.Unresolved => new Pen(Color.FromArgb(230, 230, 120, 20), 3.5f) { DashStyle = DashStyle.Dash },
                    _ => new Pen(Color.FromArgb(240, 25, 25, 35), 4f),
                };
                g.DrawLine(pen, W2S(a.X, a.Y), W2S(b.X, b.Y));
            }
            var poly = room.Polygon;
            for (int i = 0; i < poly.Count; i++)
            {
                var a = poly[i];
                var b = poly[(i + 1) % poly.Count];
                double len = Geometry.Dist(a, b);
                var sa = W2S(a.X, a.Y);
                var sb = W2S(b.X, b.Y);
                g.FillRectangle(Brushes.SteelBlue, sa.X - 3, sa.Y - 3, 6, 6);
                if (len > 300)
                    g.DrawString($"{len / 1000:0.00} m", font, Brushes.SteelBlue, (sa.X + sb.X) / 2 + 3, (sa.Y + sb.Y) / 2 + 3);
            }
            if (!room.Closed)
            {
                using var big = new Font("Segoe UI", 10, FontStyle.Bold);
                var p0 = W2S(poly[0].X, poly[0].Y);
                g.DrawString("INCOMPLETE OUTLINE", big, Brushes.Firebrick, p0.X + 6, p0.Y + 6);
            }
        }

        // Seeds
        foreach (var sd in _world.Seeds)
        {
            var p = W2S(sd.X, sd.Y);
            var diamond = new[] { new PointF(p.X, p.Y - 6), new PointF(p.X + 6, p.Y), new PointF(p.X, p.Y + 6), new PointF(p.X - 6, p.Y) };
            switch (sd.State)
            {
                case SeedState.Pending:
                    using (var pen = new Pen(Color.DarkOrange, 2f)) g.DrawPolygon(pen, diamond);
                    break;
                case SeedState.Unreachable:
                    g.FillPolygon(Brushes.Firebrick, diamond);
                    break;
                case SeedState.Ghost:
                    g.DrawEllipse(Pens.Gray, p.X - 4, p.Y - 4, 8, 8);
                    break;
                default:
                    g.FillEllipse(Brushes.Gray, p.X - 2, p.Y - 2, 4, 4);
                    break;
            }
        }
    }

    /// <summary>Key for the mapping colours (bottom right).</summary>
    private void DrawMapKey(Graphics g)
    {
        if (_world?.Room == null) return;
        var items = new (Color C, string T, bool Line)[]
        {
            (Color.FromArgb(25, 25, 35), "wall (confirmed)", true), (Color.FromArgb(30, 90, 200), "wall (inferred)", true),
            (Color.FromArgb(230, 120, 20), "wall (unresolved)", true), (Color.FromArgb(30, 160, 60), "doorway", true),
            (Color.FromArgb(60, 180, 90), "covered", false), (Color.FromArgb(80, 150, 230), "remaining", false),
            (Color.FromArgb(230, 160, 30), "skipped", false), (Color.FromArgb(120, 120, 120), "unreachable", false),
            (Color.DarkOrange, "seed to investigate", false),
        };
        int w = 150, lh = 15, x = Width - w - 20, y = Height - items.Length * lh - 20;
        using (var bg = new SolidBrush(Color.FromArgb(220, 255, 255, 255))) g.FillRectangle(bg, x - 6, y - 6, w + 12, items.Length * lh + 10);
        foreach (var (c, t, line) in items)
        {
            if (line) using (var pen = new Pen(c, 3f)) g.DrawLine(pen, x, y + 7, x + 16, y + 7);
            else using (var b = new SolidBrush(Color.FromArgb(160, c))) g.FillRectangle(b, x, y + 2, 16, 10);
            g.DrawString(t, Font, Brushes.Black, x + 22, y);
            y += lh;
        }
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
}

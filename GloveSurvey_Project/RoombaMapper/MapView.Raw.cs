using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
using RoombaMapper.Core;

namespace RoombaMapper;

/// <summary>MapView (see MapView.cs): raw layers - the path, raw IR hits and traced outlines, corners</summary>
public sealed partial class MapView
{
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
}

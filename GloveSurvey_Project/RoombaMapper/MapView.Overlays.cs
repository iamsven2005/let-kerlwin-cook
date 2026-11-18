using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
using RoombaMapper.Core;

namespace RoombaMapper;

/// <summary>MapView (see MapView.cs): overlays - planned route, home and target, the robot, scale bar</summary>
public sealed partial class MapView
{
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
}

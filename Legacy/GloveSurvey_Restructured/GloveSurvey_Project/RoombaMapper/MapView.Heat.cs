using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
using RoombaMapper.Core;

namespace RoombaMapper;

/// <summary>MapView (see MapView.cs): heat map layer - building the heat image from readings, colours, drawing it</summary>
public sealed partial class MapView
{
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

    private void DrawHeat(Graphics g)
    {
        if (_heat == null) return;
        var tl = W2S(_heatMinX, _heatMaxY);
        var dest = new Rectangle(
            (int)Math.Round(tl.X), (int)Math.Round(tl.Y),
            (int)Math.Round(_heatW * CellMm * _scale), (int)Math.Round(_heatH * CellMm * _scale));

        // Keep the heat inside the room walls once the room has been mapped
        using var roomClip = new GraphicsPath();
        var clipPoly = _world?.Room is { Corners.Count: >= 3 } wr ? wr.Polygon
                     : _roomShapeId is int rid && _corners.TryGetValue(rid, out var room) && room.Count >= 3 ? room : null;
        if (clipPoly != null)
        {
            roomClip.AddPolygon(clipPoly.Select(p => W2S(p.X, p.Y)).ToArray());
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
}

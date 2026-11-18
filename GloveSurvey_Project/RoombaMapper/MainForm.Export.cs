using System.Drawing.Imaging;
using System.Globalization;
using System.Text;

namespace RoombaMapper;

/// <summary>
/// Features "Export CSV" and "Save image" (Features.ExportCsv, Features.SaveImage): their
/// top-bar buttons. The heat-map export at the end of a survey (ExportHeatmap) is always
/// available to the mission and the glove's survey guide.
/// </summary>
public sealed partial class MainForm
{
    private readonly Button _exportCsv = new() { Text = "Export CSV", AutoSize = true };
    private readonly Button _savePng = new() { Text = "Save image", AutoSize = true };

    private void BuildExportCsvButton()
    {
        _top.Controls.Add(_exportCsv);
        _exportCsv.Click += (_, _) => ExportCsv();
    }

    private void BuildSaveImageButton()
    {
        _top.Controls.Add(_savePng);
        _savePng.Click += (_, _) => SavePng();
    }

    private void ExportCsv()
    {
        if (_map.Samples.Count == 0 && _map.Readings.Count == 0) { SetStatus("Nothing to export yet."); return; }
        using var dlg = new SaveFileDialog { Filter = "CSV files|*.csv", FileName = $"wifi_survey_{DateTime.Now:yyyyMMdd_HHmm}.csv" };
        if (dlg.ShowDialog(this) != DialogResult.OK) return;
        File.WriteAllText(dlg.FileName, BuildCsv());
        SetStatus($"Saved {_map.Samples.Count} positions and {_map.Readings.Count} Wi-Fi readings to {Path.GetFileName(dlg.FileName)}");
    }

    private string BuildCsv()
    {
        var inv = CultureInfo.InvariantCulture;
        static string B(bool? b) => b switch { true => "1", false => "0", null => "" };
        var sb = new StringBuilder("type,time_ms,x_mm,y_mm,heading_deg,distance_mm,rssi_dbm,ir_front,ir_left,ir_right,shape_id\n");
        foreach (var t in _map.Samples)
            sb.AppendLine(string.Join(",", "P",
                t.TimeMs.ToString(inv), t.X.ToString("0.#", inv), t.Y.ToString("0.#", inv),
                t.HeadingDeg.ToString("0.#", inv), t.DistanceMm.ToString("0.#", inv),
                t.Rssi?.ToString("0", inv) ?? "",
                B(t.Ir), B(t.IrLeft), B(t.IrRight), ""));
        foreach (var r in _map.Readings)   // Wi-Fi readings (R): rssi at x,y; ir columns unused
            sb.AppendLine(string.Join(",", "R", r.Seq.ToString(inv),
                r.X.ToString("0.#", inv), r.Y.ToString("0.#", inv), "", "",
                r.Rssi.ToString(inv), "", "", "", r.Mode.ToString()));
        foreach (var o in _map.Obstacles)
            sb.AppendLine(string.Join(",", "O", o.TimeMs.ToString(inv),
                o.X.ToString("0.#", inv), o.Y.ToString("0.#", inv), "", "", "", "", "", "",
                o.ShapeId.ToString(inv)));
        return sb.ToString();
    }

    /// <summary>
    /// Saves the heat map as a picture (whole room in view, without the planned route
    /// or target) plus the readings as a CSV with the same name.
    /// </summary>
    private void ExportHeatmap()
    {
        using var dlg = new SaveFileDialog
        {
            Filter = "PNG image|*.png", FileName = $"wifi_heatmap_{DateTime.Now:yyyyMMdd_HHmm}.png",
            Title = "Export heat map (the readings are saved as a CSV next to it)"
        };
        if (dlg.ShowDialog(this) != DialogResult.OK) return;

        var route = _map.PlannedPath;
        var target = _map.Target;
        _map.PlannedPath = null;
        _map.Target = null;
        _map.FitToData();
        try
        {
            using var bmp = new Bitmap(_map.Width, _map.Height);
            _map.DrawToBitmap(bmp, new Rectangle(0, 0, bmp.Width, bmp.Height));
            bmp.Save(dlg.FileName, ImageFormat.Png);
        }
        finally
        {
            _map.PlannedPath = route;
            _map.Target = target;
        }
        string csvPath = Path.ChangeExtension(dlg.FileName, ".csv");
        File.WriteAllText(csvPath, BuildCsv());
        SetStatus($"Heat map saved: {Path.GetFileName(dlg.FileName)} and {Path.GetFileName(csvPath)}");
    }

    private void SavePng()
    {
        using var dlg = new SaveFileDialog { Filter = "PNG image|*.png", FileName = $"wifi_heatmap_{DateTime.Now:yyyyMMdd_HHmm}.png" };
        if (dlg.ShowDialog(this) != DialogResult.OK) return;

        using var bmp = new Bitmap(_map.Width, _map.Height);
        _map.DrawToBitmap(bmp, new Rectangle(0, 0, bmp.Width, bmp.Height));
        bmp.Save(dlg.FileName, ImageFormat.Png);
        SetStatus($"Saved image {Path.GetFileName(dlg.FileName)}");
    }
}

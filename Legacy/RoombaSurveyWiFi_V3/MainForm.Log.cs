using System.Runtime.InteropServices;
using RoombaMapper.Ui;

namespace RoombaMapper;

/// <summary>
/// The event log under the map. The original log box (_log) still gets every line exactly
/// as before. Searching and filtering read a separate copy of the most recent lines and show
/// the matches in a second box, so a filter never hides or drops anything from _log.
/// </summary>
public sealed partial class MainForm
{
    private const int LogKeepLines = 5000;
    private readonly List<string> _logLines = new();
    private readonly Panel _logPanel = new() { Dock = DockStyle.Bottom, Height = 150, BackColor = Theme.SurfaceAlt };
    private readonly Splitter _logSplitter = new() { Dock = DockStyle.Bottom, Height = 4, BackColor = Theme.Border };
    private readonly FlowLayoutPanel _logHeader = new()
    {
        Dock = DockStyle.Top, AutoSize = true, WrapContents = true, BackColor = Theme.Surface, Padding = new Padding(8, 3, 8, 3)
    };
    private readonly TextBox _logView = new()
    {
        Dock = DockStyle.Fill, Multiline = true, ReadOnly = true, BorderStyle = BorderStyle.None, Visible = false,
        ScrollBars = ScrollBars.Vertical, Font = new Font("Consolas", 9), BackColor = Theme.SurfaceAlt
    };
    private readonly ComboBox _logKind = new() { DropDownStyle = ComboBoxStyle.DropDownList, Width = 150 };
    private readonly TextBox _logSearch = new() { Width = 170, PlaceholderText = "Search log" };
    private readonly Button _logCopy = new() { Text = "Copy", AutoSize = true };
    private readonly Button _logToggle = new() { Text = "Hide log", AutoSize = true };
    private readonly Label _logInfo = new() { AutoSize = true, ForeColor = Theme.TextMuted, Margin = new Padding(8, 8, 3, 3) };
    private int _logOpenHeight;

    private Control BuildLogPanel(Control center)
    {
        _logKind.Items.AddRange(new object[] { "All lines", "Status and errors", "Commands sent", "Mapping and glove", "Robot lines" });
        _logKind.SelectedIndex = 0;
        var title = new Label { Text = "Event log", AutoSize = true, Font = Theme.BodySemibold, ForeColor = Theme.Text, Margin = new Padding(0, 8, 10, 3) };
        // The map's cursor position lives here, so showing it can't move the map
        _logHeader.Controls.AddRange(new Control[] { title, _logKind, _logSearch, _logCopy, _logToggle, _logInfo, _cursorLbl });
        _logHeader.Paint += (_, e) =>
        {
            using var p = new Pen(Theme.Border);
            e.Graphics.DrawLine(p, 0, _logHeader.Height - 1, _logHeader.Width, _logHeader.Height - 1);
        };
        _logPanel.Controls.Add(_log);          // Fill first, for docking
        _logPanel.Controls.Add(_logView);
        _logPanel.Controls.Add(_logHeader);
        _logSplitter.MinSize = Theme.Px(center, 80);
        _logSplitter.MinExtra = Theme.Px(center, 220);   // keeps the map at least this tall
        center.Controls.Add(_logSplitter);

        _tips.SetToolTip(_logKind, "Show one kind of line. The full log is kept either way.");
        _tips.SetToolTip(_logSearch, $"Search the last {LogKeepLines:N0} lines (not case-sensitive)");
        _tips.SetToolTip(_logCopy, "Copy the selected text, or every line shown, to the clipboard");

        _logKind.SelectedIndexChanged += (_, _) => RefreshLogView();
        _logSearch.TextChanged += (_, _) => RefreshLogView();
        _logCopy.Click += (_, _) => CopyLog();
        _logToggle.Click += (_, _) => SetLogOpen(_logToggle.Text != "Hide log");
        return _logPanel;
    }

    /// <summary>1 status/errors, 2 commands sent, 3 mapping/glove notes, 4 lines from the robot.</summary>
    private static int LogKindOf(string line) =>
        line.StartsWith("[status] ") ? 1 : line.StartsWith("> ") ? 2
        : line.StartsWith("[map] ") || line.StartsWith("[glove] ") ? 3 : 4;

    private bool LogFilterActive => _logKind.SelectedIndex > 0 || _logSearch.Text.Length > 0;

    private bool LogMatches(string line) =>
        (_logKind.SelectedIndex <= 0 || LogKindOf(line) == _logKind.SelectedIndex) &&
        (_logSearch.Text.Length == 0 || line.Contains(_logSearch.Text, StringComparison.OrdinalIgnoreCase));

    private void LogViewOnLine(string line)
    {
        _logLines.Add(line);
        if (_logLines.Count > LogKeepLines + 500) _logLines.RemoveRange(0, _logLines.Count - LogKeepLines);
        if (!LogFilterActive || !LogMatches(line)) return;
        if (_logView.TextLength > 60000) RefreshLogView();
        else _logView.AppendText(line + Environment.NewLine);
    }

    private void RefreshLogView()
    {
        bool active = LogFilterActive;
        _log.Visible = !active;
        _logView.Visible = active;
        if (!active) { _logInfo.Text = ""; return; }
        var matches = _logLines.Where(LogMatches).ToList();
        var shown = matches.Count > 2000 ? matches.GetRange(matches.Count - 2000, 2000) : matches;
        _logView.Text = shown.Count == 0 ? "" : string.Join(Environment.NewLine, shown) + Environment.NewLine;
        _logView.SelectionStart = _logView.TextLength;
        _logView.ScrollToCaret();
        _logInfo.Text = matches.Count == shown.Count
            ? $"{matches.Count:N0} of {_logLines.Count:N0} lines match"
            : $"{matches.Count:N0} of {_logLines.Count:N0} lines match (last {shown.Count:N0} shown)";
    }

    private void CopyLog()
    {
        var box = _logView.Visible ? _logView : _log;
        string text = box.SelectionLength > 0 ? box.SelectedText : box.Text;
        if (text.Length == 0) { _logInfo.Text = "Nothing to copy"; return; }
        try
        {
            Clipboard.SetText(text);
            _logInfo.Text = box.SelectionLength > 0 ? "Selection copied" : "Copied the lines shown";
        }
        catch (ExternalException ex)
        {
            SetStatus("Couldn't copy the log: the clipboard is busy (" + ex.Message.Trim() + ")");
        }
    }

    private void SetLogOpen(bool open)
    {
        if (open)
        {
            _logPanel.Height = Math.Max(_logOpenHeight, Theme.Px(this, 120));
            _logToggle.Text = "Hide log";
        }
        else
        {
            _logOpenHeight = _logPanel.Height;
            _logPanel.Height = _logHeader.Height;
            _logToggle.Text = "Show log";
        }
        _logSplitter.Visible = open;
    }
}

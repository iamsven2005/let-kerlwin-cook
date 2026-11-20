namespace RoombaMapper;

/// <summary>
/// Feature "Debug panel" (Features.DebugPanel): a "Debug" button in the top bar opens a
/// window with one card per glove feature, showing that feature's debug page.
///   - Opening the window asks the glove to send its pages (MIRROR,ON); hiding it stops them.
///   - Cards appear in the order of the glove's feature list (its F line). A card goes grey
///     when its page hasn't been updated for 3 s.
///   - Clicking a card shows that page on the glove's own screen (DEBUG,&lt;feature&gt;).
///   - "Record to CSV" saves every page with a timestamp, to look at a walk afterwards.
/// The glove needs DEBUG_TO_APP (and DEBUG_SCREEN for the glove-screen buttons) in its
/// features.h. The Simulation source sends a page too, so the window can be tried without
/// hardware.
/// </summary>
public sealed partial class MainForm
{
    private readonly Button _debugBtn = new() { Text = "Debug", AutoSize = true };
    private DebugWindow? _debugWindow;

    private void BuildDebugButton()
    {
        _top.Controls.Add(_debugBtn);
        _debugBtn.Click += (_, _) =>
        {
            _debugWindow ??= new DebugWindow(this);
            if (_debugWindow.Visible) _debugWindow.Hide();
            else _debugWindow.Show(this);
        };
    }

    private void DebugOnFeatureLine(string line) => _debugWindow?.OnLine(line);

    private void DebugOnDisconnected() => _debugWindow?.OnDisconnected();

    // ---------------------------------------------------------------- the window

    private sealed class DebugWindow : Form
    {
        private const double StaleSeconds = 3;

        private readonly MainForm _f;
        private readonly CheckBox _mirror = new() { Text = "Send pages from the glove", AutoSize = true, Margin = new Padding(6, 6, 3, 3) };
        private readonly NumericUpDown _every = new() { Minimum = 0.2m, Maximum = 60, DecimalPlaces = 1, Increment = 0.5m, Value = 1, Width = 55 };
        private readonly CheckBox _record = new() { Text = "Record to CSV", AutoSize = true, Margin = new Padding(12, 6, 3, 3) };
        private readonly Button _viewOn = new() { Text = "Glove screen: debug view", AutoSize = true };
        private readonly Button _viewOff = new() { Text = "Glove screen: normal view", AutoSize = true };
        private readonly Label _info = new() { AutoSize = true, ForeColor = Color.DimGray, Margin = new Padding(12, 7, 3, 3), Text = "No pages yet" };
        private readonly FlowLayoutPanel _cards = new()
        {
            Dock = DockStyle.Fill, AutoScroll = true, Padding = new Padding(6), BackColor = Color.FromArgb(250, 250, 250)
        };
        private readonly System.Windows.Forms.Timer _ageTimer = new() { Interval = 500 };
        private readonly Dictionary<string, Card> _byName = new();
        private List<string> _order = new();
        private StreamWriter? _writer;
        private bool _syncing;

        public DebugWindow(MainForm f)
        {
            _f = f;
            Text = "Glove debug pages";
            Size = new Size(760, 560);
            StartPosition = FormStartPosition.CenterParent;
            Font = f.Font;

            var bar = new FlowLayoutPanel { Dock = DockStyle.Top, AutoSize = true, Padding = new Padding(6), WrapContents = true };
            bar.Controls.AddRange(new Control[] { _mirror, Caption("every"), _every, Caption("s"), _record, _viewOn, _viewOff, _info });
            Controls.Add(_cards);      // Fill first, for docking
            Controls.Add(bar);

            var tip = new ToolTip();
            tip.SetToolTip(_mirror, "The glove sends every feature's page this often (MIRROR,ON / OFF)");
            tip.SetToolTip(_viewOn, "Switch the glove's own screen to its debug pages (DEBUG,ON)");
            tip.SetToolTip(_viewOff, "Switch the glove's own screen back to the normal view (DEBUG,OFF)");

            _mirror.CheckedChanged += (_, _) => { if (!_syncing) SendMirror(); };
            _every.ValueChanged += (_, _) => Send($"SET,dbg_ms,{(int)(_every.Value * 1000)}", "Debug page rate");
            _viewOn.Click += (_, _) => Send("DEBUG,ON", "Glove debug view");
            _viewOff.Click += (_, _) => Send("DEBUG,OFF", "Glove normal view");
            _record.CheckedChanged += (_, _) => { if (!_syncing) ToggleRecording(); };
            _ageTimer.Tick += (_, _) => { foreach (var c in _byName.Values) c.UpdateAge(StaleSeconds); };

            // Shown: ask for pages. Hidden: stop them (no traffic while nobody looks).
            VisibleChanged += (_, _) =>
            {
                SetMirror(Visible);
                if (Visible) _ageTimer.Start(); else _ageTimer.Stop();
            };
            FormClosing += (_, e) =>
            {
                if (e.CloseReason != CloseReason.UserClosing) return;
                e.Cancel = true;       // keep the cards: just hide
                Hide();
            };
        }

        private bool Connected => _f._active?.CanSend == true;

        private void Send(string command, string label)
        {
            if (Connected) _f.SendCommand(command, label);
        }

        private void SetMirror(bool on)
        {
            _syncing = true;
            _mirror.Checked = on;
            _syncing = false;
            SendMirror();
        }

        private void SendMirror() => Send(_mirror.Checked ? "MIRROR,ON" : "MIRROR,OFF", "Debug pages");

        public void OnLine(string line)
        {
            line = line.TrimEnd('\r', '\n');
            if (line.StartsWith("F,"))
            {
                _order = line.Split(',').Skip(1).Where(n => !n.Contains('=')).ToList();
                _info.Text = $"{_order.Count} features in the glove";
                Reorder();
                if (Visible && _mirror.Checked) SendMirror();     // the glove may have restarted
                return;
            }
            if (!line.StartsWith("D,")) return;
            int comma = line.IndexOf(',', 2);
            if (comma < 0) return;
            string name = line[2..comma];
            string text = line[(comma + 1)..];

            if (!_byName.TryGetValue(name, out var card))
            {
                card = new Card(name, () => Send($"DEBUG,{name}", "Show debug page"));
                _byName[name] = card;
                _cards.Controls.Add(card.Panel);
                Reorder();
                _info.Text = $"{_byName.Count} pages";
            }
            card.Set(text.Replace('|', '\n'));

            if (_writer != null)
                _writer.WriteLine($"{DateTime.Now:yyyy-MM-dd HH:mm:ss.fff},{name},\"{text.Replace("\"", "\"\"")}\"");
        }

        public void OnDisconnected() => _info.Text = "Disconnected: pages are no longer updating";

        private void Reorder()
        {
            int Rank(string n) { int i = _order.IndexOf(n); return i < 0 ? int.MaxValue : i; }
            int k = 0;
            foreach (var c in _byName.Values.OrderBy(c => Rank(c.Name)).ThenBy(c => c.Name))
                _cards.Controls.SetChildIndex(c.Panel, k++);
        }

        private void ToggleRecording()
        {
            if (!_record.Checked)
            {
                _writer?.Dispose();
                _writer = null;
                return;
            }
            using var dlg = new SaveFileDialog
            {
                Filter = "CSV files|*.csv", FileName = $"glove_debug_{DateTime.Now:yyyyMMdd_HHmm}.csv",
                Title = "Record the glove's debug pages"
            };
            if (dlg.ShowDialog(this) != DialogResult.OK)
            {
                _syncing = true;
                _record.Checked = false;
                _syncing = false;
                return;
            }
            _writer = new StreamWriter(dlg.FileName) { AutoFlush = true };
            _writer.WriteLine("pc_time,feature,page");
        }

        protected override void Dispose(bool disposing)
        {
            if (disposing)
            {
                _writer?.Dispose();
                _ageTimer.Dispose();
            }
            base.Dispose(disposing);
        }
    }

    /// <summary>One feature's page: name, the page's lines, and how old it is.</summary>
    private sealed class Card
    {
        public Panel Panel { get; } = new()
        {
            Width = 230, Height = 118, BorderStyle = BorderStyle.FixedSingle, Margin = new Padding(4),
            BackColor = Color.White, Cursor = Cursors.Hand
        };
        private readonly Label _title = new() { Dock = DockStyle.Top, Height = 20, Font = new Font("Segoe UI", 9, FontStyle.Bold), Padding = new Padding(4, 3, 0, 0) };
        private readonly Label _body = new() { Dock = DockStyle.Fill, Font = new Font("Consolas", 9), Padding = new Padding(4, 2, 0, 0) };
        private readonly Label _age = new() { Dock = DockStyle.Bottom, Height = 16, ForeColor = Color.Gray, Padding = new Padding(4, 0, 0, 0) };
        private DateTime _last = DateTime.MinValue;

        public string Name { get; }

        public Card(string name, Action click)
        {
            Name = name;
            _title.Text = name;
            Panel.Controls.Add(_body);     // Fill first, for docking
            Panel.Controls.Add(_age);
            Panel.Controls.Add(_title);
            foreach (var c in new Control[] { Panel, _title, _body, _age }) c.Click += (_, _) => click();
            new ToolTip().SetToolTip(_title, "Click to show this page on the glove's screen");
        }

        public void Set(string text)
        {
            _body.Text = text;
            _last = DateTime.Now;
            UpdateAge(double.MaxValue);
        }

        public void UpdateAge(double staleSeconds)
        {
            bool never = _last == DateTime.MinValue;
            double s = (DateTime.Now - _last).TotalSeconds;
            bool stale = never || s > staleSeconds;
            _age.Text = never ? "no data yet" : s < 1 ? "just now" : $"{s:0} s ago";
            Panel.BackColor = stale ? Color.FromArgb(235, 235, 235) : Color.White;
            _body.ForeColor = stale ? Color.Gray : Color.Black;
        }
    }
}

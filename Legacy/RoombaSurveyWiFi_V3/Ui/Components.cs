using System.Drawing.Drawing2D;

namespace RoombaMapper.Ui;

/// <summary>
/// A titled card for a page or the inspector. Dock it Top inside a scrolling panel: it
/// grows to fit its body. Add content with <see cref="Add"/> (top to bottom).
/// </summary>
public sealed class UiSection : Panel
{
    private const int Gap = 10;                       // space below the card
    private readonly Label _title;
    private readonly Panel _body = new() { Dock = DockStyle.Top, AutoSize = true, AutoSizeMode = AutoSizeMode.GrowAndShrink };
    private readonly List<Control> _items = new();

    public UiSection(string title, string? description = null)
    {
        SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw, true);
        Dock = DockStyle.Top;
        AutoSize = true;
        AutoSizeMode = AutoSizeMode.GrowAndShrink;
        BackColor = Theme.Canvas;
        Padding = new Padding(12, 10, 12, 12 + Gap);
        _body.BackColor = Theme.Surface;

        _title = new Label
        {
            Text = title, Dock = DockStyle.Top, AutoSize = false, Height = 22, Font = Theme.SectionTitle,
            ForeColor = Theme.Text, BackColor = Theme.Surface, TextAlign = ContentAlignment.MiddleLeft
        };
        Controls.Add(_body);
        if (description != null)
        {
            var d = Theme.Wrapping(new Label
            {
                Text = description, Dock = DockStyle.Top, ForeColor = Theme.TextSecondary,
                BackColor = Theme.Surface, Font = Theme.Small, Padding = new Padding(0, 0, 0, 6)
            });
            Controls.Add(d);
        }
        Controls.Add(_title);
    }

    public string Title { get => _title.Text; set => _title.Text = value; }

    /// <summary>Adds a control under the previous one, stretched to the card's width.</summary>
    public T Add<T>(T control) where T : Control
    {
        control.Dock = DockStyle.Top;
        if (control.BackColor == SystemColors.Control || control is FlowLayoutPanel or TableLayoutPanel)
            control.BackColor = Theme.Surface;
        _items.Add(control);
        _body.SuspendLayout();
        _body.Controls.Clear();
        for (int i = _items.Count - 1; i >= 0; i--) _body.Controls.Add(_items[i]);   // Dock Top: last added = top
        _body.ResumeLayout(true);
        return control;
    }

    protected override void OnPaintBackground(PaintEventArgs e)
    {
        base.OnPaintBackground(e);
        var r = new Rectangle(0, 0, Width - 1, Height - 1 - Theme.Px(this, Gap));
        if (r.Height <= 0) return;
        using var fill = new SolidBrush(Theme.Surface);
        using var pen = new Pen(Theme.Border);
        e.Graphics.FillRectangle(fill, r);
        e.Graphics.DrawRectangle(pen, r);
    }
}

/// <summary>
/// Caption/value rows: caption on the left, value on the right. Label values wrap onto more
/// lines instead of being cut off; inputs keep their own width. Lays itself out (a
/// TableLayoutPanel doesn't reliably wrap auto-sized labels).
/// </summary>
public sealed class PropertyTable : Panel
{
    private readonly int _captionWidth;
    private readonly List<(Label Caption, Control Value, Control? Leading, int DesignWidth)> _rows = new();
    private bool _laying;

    public PropertyTable(int captionWidth = 96)
    {
        _captionWidth = captionWidth;
        BackColor = Theme.Surface;
        Margin = Padding.Empty;
        Padding = Padding.Empty;
    }

    /// <summary>Adds a row; <paramref name="leading"/> is an optional small marker before the value.</summary>
    public Label AddRow(string caption, Control value, string? tooltip = null, ToolTip? tip = null, Control? leading = null)
    {
        var cap = new Label { Text = caption, AutoSize = false, ForeColor = Theme.TextSecondary, Font = Theme.Body };
        if (value is Label l) l.AutoSize = false;
        Controls.Add(cap);
        if (leading != null) Controls.Add(leading);
        Controls.Add(value);
        value.TextChanged += (_, _) => PerformLayout();
        value.FontChanged += (_, _) => PerformLayout();
        _rows.Add((cap, value, leading, value.Width));   // inputs keep this (96-dpi) width when there's room
        if (tooltip != null && tip != null) { tip.SetToolTip(cap, tooltip); tip.SetToolTip(value, tooltip); }
        PerformLayout();
        return cap;
    }

    private static int TextHeight(Control c, int width) =>
        TextRenderer.MeasureText(c.Text.Length == 0 ? " " : c.Text, c.Font, new Size(Math.Max(1, width), int.MaxValue),
                                 TextFormatFlags.WordBreak | TextFormatFlags.TextBoxControl).Height;

    protected override void OnLayout(LayoutEventArgs e)
    {
        base.OnLayout(e);
        if (_laying || Width <= 0) return;
        _laying = true;
        try
        {
            int capW = Theme.Px(this, _captionWidth), gap = Theme.Px(this, 4), y = 0;
            foreach (var (cap, value, leading, designWidth) in _rows)
            {
                int x = capW;
                if (leading != null)
                {
                    leading.Location = new Point(x, y + Theme.Px(this, 5));
                    x += leading.Width + Theme.Px(this, 6);
                }
                int valueW = Math.Max(20, Width - x);
                int valueH, capTop = y + Theme.Px(this, 3);
                if (value is Label)
                {
                    valueH = TextHeight(value, valueW) + Theme.Px(this, 4);
                    value.SetBounds(x, y + Theme.Px(this, 2), valueW, valueH);
                }
                else
                {
                    int w = value is CheckBox ? valueW : Math.Min(Theme.Px(this, designWidth), valueW);
                    value.SetBounds(x, y + Theme.Px(this, 1), w, value.Height);
                    valueH = value.Height + Theme.Px(this, 2);
                    capTop = y + Math.Max(0, (value.Height - TextHeight(cap, capW)) / 2) + Theme.Px(this, 1);
                }
                int capH = TextHeight(cap, capW - gap);
                cap.SetBounds(0, capTop, capW - gap, capH);
                y += Math.Max(valueH, capTop - y + capH) + gap;
            }
            if (Height != y) Height = y;
        }
        finally { _laying = false; }
    }
}

/// <summary>A rounded status badge (REAL HARDWARE / SIMULATION / NOT CONNECTED).</summary>
public sealed class Pill : Control
{
    private Color _fill = Theme.Neutral;
    public Pill()
    {
        SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer |
                 ControlStyles.UserPaint | ControlStyles.ResizeRedraw | ControlStyles.SupportsTransparentBackColor, true);
        Font = Theme.SmallSemibold;
        ForeColor = Color.White;
        Margin = new Padding(8, 0, 8, 0);
        TabStop = false;
    }

    public Color Fill { get => _fill; set { _fill = value; Invalidate(); } }

    protected override void OnTextChanged(EventArgs e)
    {
        base.OnTextChanged(e);
        FitText();
    }

    // Sized from its text at the current DPI, so DPI scaling must not enlarge it again
    protected override void ScaleControl(SizeF factor, BoundsSpecified specified)
    {
        base.ScaleControl(factor, specified);
        FitText();
    }

    private void FitText()
    {
        var s = TextRenderer.MeasureText(Text, Font);
        Size = new Size(s.Width + Theme.Px(this, 18), s.Height + Theme.Px(this, 8));
        Invalidate();
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        var g = e.Graphics;
        g.SmoothingMode = SmoothingMode.AntiAlias;
        var r = new RectangleF(0.5f, 0.5f, Width - 1.5f, Height - 1.5f);
        float d = r.Height;
        using var path = new GraphicsPath();
        path.AddArc(r.X, r.Y, d, d, 90, 180);
        path.AddArc(r.Right - d, r.Y, d, d, 270, 180);
        path.CloseFigure();
        using var b = new SolidBrush(_fill);
        g.FillPath(b, path);
        TextRenderer.DrawText(g, Text, Font, ClientRectangle, ForeColor,
                              TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter | TextFormatFlags.NoPadding);
    }
}

/// <summary>A small square showing a colour (the RSSI's heat-map colour next to its value).</summary>
public sealed class Swatch : Control
{
    public Swatch()
    {
        SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer, true);
        Size = new Size(12, 12);
        TabStop = false;
    }

    /// <summary>The colour shown, or null for "no reading" (drawn as an empty outline).</summary>
    public Color? Value { get => _value; set { _value = value; Invalidate(); } }
    private Color? _value;

    protected override void OnPaint(PaintEventArgs e)
    {
        e.Graphics.Clear(Parent?.BackColor ?? Theme.Surface);
        var r = new Rectangle(0, 0, Width - 1, Height - 1);
        if (_value is Color c) using (var b = new SolidBrush(c)) e.Graphics.FillRectangle(b, r);
        using var pen = new Pen(Theme.BorderStrong);
        e.Graphics.DrawRectangle(pen, r);
    }
}

/// <summary>
/// The heat map's colour scale, drawn with the map's own colour function and limits, plus
/// the app's signal-quality bands. Nothing here is computed separately from the map.
/// </summary>
public sealed class RssiLegend : Control
{
    private readonly MapView _map;
    private readonly (double Dbm, string Label)[] _bands;

    public RssiLegend(MapView map, params (double Dbm, string Label)[] qualityBands)
    {
        _map = map;
        _bands = qualityBands;
        SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw, true);
        Font = Theme.Small;
        Height = 58;
        TabStop = false;
        BackColor = Theme.Surface;
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        var g = e.Graphics;
        g.Clear(BackColor);
        double weak = _map.RssiWeak, strong = _map.RssiStrong;
        int pad = Theme.Px(this, 2);
        int barH = Theme.Px(this, 12);
        int x0 = pad, w = Math.Max(10, Width - 2 * pad), y = Theme.Px(this, 2);
        for (int i = 0; i < w; i++)
        {
            double rssi = weak + (strong - weak) * i / (w - 1);
            using var p = new Pen(_map.ColorFor(rssi));
            g.DrawLine(p, x0 + i, y, x0 + i, y + barH);
        }
        using (var border = new Pen(Theme.BorderStrong)) g.DrawRectangle(border, x0, y, w - 1, barH);

        float X(double dbm) => (float)(x0 + (dbm - weak) / (strong - weak) * (w - 1));
        int ty = y + barH + Theme.Px(this, 2);
        var flags = TextFormatFlags.NoPadding;
        TextRenderer.DrawText(g, $"{weak:0}", Font, new Point(x0, ty), Theme.TextSecondary, flags);
        var strongText = $"{strong:0} dBm";
        var ss = TextRenderer.MeasureText(strongText, Font, Size.Empty, flags);
        TextRenderer.DrawText(g, strongText, Font, new Point(x0 + w - ss.Width, ty), Theme.TextSecondary, flags);

        // Quality bands (the same thresholds the inspector uses for its quality word)
        int by = ty + ss.Height + Theme.Px(this, 4);
        using var tick = new Pen(Theme.Text);
        foreach (var (dbm, label) in _bands)
        {
            if (dbm <= weak || dbm >= strong) continue;
            float tx = X(dbm);
            g.DrawLine(tick, tx, y - 1, tx, y + barH + 1);
        }
        string text = string.Join("   ", _bands.Select(b => $"{b.Label} ≥ {b.Dbm:0}")) + "   below: Weak";
        TextRenderer.DrawText(g, text, Font, new Rectangle(x0, by, w, Height - by), Theme.TextMuted,
                              TextFormatFlags.WordBreak | TextFormatFlags.NoPadding);
    }
}

/// <summary>
/// The left navigation rail. Pages are kept alive and only shown/hidden, so switching never
/// touches the connection, map or running phase. Clicking the active page hides the panel.
/// </summary>
public sealed class NavRail : Panel
{
    private readonly List<NavItem> _items = new();
    public event Action<int>? SelectedChanged;
    public event Action? ActiveItemClicked;

    public NavRail()
    {
        Dock = DockStyle.Left;
        BackColor = Theme.Chrome;
        Padding = new Padding(0, 8, 0, 0);
        Width = 100;
    }

    public int SelectedIndex => _items.FindIndex(i => i.Checked);

    public void AddItem(string text, string tooltip, ToolTip tip)
    {
        var item = new NavItem { Text = text, Dock = DockStyle.Top, Height = 38, AccessibleName = text + " page" };
        tip.SetToolTip(item, tooltip);
        int index = _items.Count;
        bool wasChecked = false;
        item.MouseDown += (_, _) => wasChecked = item.Checked;
        item.KeyDown += (_, e) => { if (e.KeyCode == Keys.Space) wasChecked = item.Checked; };
        item.Click += (_, _) => { if (wasChecked) ActiveItemClicked?.Invoke(); wasChecked = false; };
        item.CheckedChanged += (_, _) => { if (item.Checked) SelectedChanged?.Invoke(index); };
        _items.Add(item);
        Controls.Add(item);
        Controls.SetChildIndex(item, 0);                // Dock Top: newest at the bottom of the list
    }

    public void Select(int index) => _items[index].Checked = true;

    private sealed class NavItem : RadioButton
    {
        private bool _hover;
        public NavItem()
        {
            SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer, true);
            Appearance = Appearance.Button;
            Font = Theme.Body;
            Cursor = Cursors.Hand;
        }
        protected override void OnMouseEnter(EventArgs e) { _hover = true; Invalidate(); base.OnMouseEnter(e); }
        protected override void OnMouseLeave(EventArgs e) { _hover = false; Invalidate(); base.OnMouseLeave(e); }
        protected override void OnCheckedChanged(EventArgs e) { base.OnCheckedChanged(e); Invalidate(); }

        protected override void OnPaint(PaintEventArgs e)
        {
            var g = e.Graphics;
            g.Clear(Checked ? Theme.ChromeRaised : _hover ? Theme.ChromeHover : Theme.Chrome);
            if (Checked)
                using (var b = new SolidBrush(Theme.Accent)) g.FillRectangle(b, 0, 0, Theme.Px(this, 3), Height);
            var r = new Rectangle(Theme.Px(this, 14), 0, Width - Theme.Px(this, 16), Height);
            TextRenderer.DrawText(g, Text, Checked ? Theme.BodySemibold : Theme.Body, r,
                                  Checked ? Color.White : Theme.ChromeMuted,
                                  TextFormatFlags.VerticalCenter | TextFormatFlags.Left | TextFormatFlags.EndEllipsis);
            if (Focused && ShowFocusCues)
                ControlPaint.DrawFocusRectangle(g, Rectangle.Inflate(ClientRectangle, -3, -3), Theme.ChromeText, Theme.Chrome);
        }
    }
}

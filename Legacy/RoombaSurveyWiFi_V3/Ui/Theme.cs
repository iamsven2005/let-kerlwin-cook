namespace RoombaMapper.Ui;

/// <summary>
/// V3 design tokens and styling helpers. Every colour and font the shell uses comes from
/// here, so the look can be changed in one place. Styling only: nothing in this file
/// touches robot commands, telemetry or the map's data.
/// </summary>
public static class Theme
{
    // ---- chrome (header and navigation rail)
    public static readonly Color Chrome = Color.FromArgb(28, 39, 52);
    public static readonly Color ChromeRaised = Color.FromArgb(40, 54, 70);
    public static readonly Color ChromeHover = Color.FromArgb(48, 63, 81);
    public static readonly Color ChromeBorder = Color.FromArgb(64, 80, 99);
    public static readonly Color ChromeText = Color.FromArgb(232, 238, 244);
    public static readonly Color ChromeMuted = Color.FromArgb(152, 167, 184);

    // ---- work surfaces
    public static readonly Color Canvas = Color.FromArgb(240, 243, 246);    // behind cards
    public static readonly Color Surface = Color.White;                     // cards, bars
    public static readonly Color SurfaceAlt = Color.FromArgb(248, 250, 251);
    public static readonly Color Border = Color.FromArgb(214, 220, 227);
    public static readonly Color BorderStrong = Color.FromArgb(188, 197, 207);
    public static readonly Color Hover = Color.FromArgb(236, 241, 245);
    public static readonly Color Pressed = Color.FromArgb(224, 231, 237);

    // ---- text
    public static readonly Color Text = Color.FromArgb(31, 41, 51);
    public static readonly Color TextSecondary = Color.FromArgb(82, 96, 109);
    public static readonly Color TextMuted = Color.FromArgb(124, 136, 148);

    // ---- meaning
    public static readonly Color Accent = Color.FromArgb(13, 128, 143);         // active / primary
    public static readonly Color AccentHover = Color.FromArgb(10, 108, 121);
    public static readonly Color AccentSoft = Color.FromArgb(223, 242, 244);
    public static readonly Color Caution = Color.FromArgb(166, 104, 0);         // text on light
    public static readonly Color CautionSoft = Color.FromArgb(255, 244, 222);
    public static readonly Color CautionBorder = Color.FromArgb(236, 196, 120);
    public static readonly Color CautionStrong = Color.FromArgb(232, 163, 23);  // fill on dark
    public static readonly Color Danger = Color.FromArgb(198, 40, 40);          // STOP and errors only
    public static readonly Color DangerHover = Color.FromArgb(168, 30, 30);
    public static readonly Color Ok = Color.FromArgb(46, 125, 79);
    public static readonly Color OkBright = Color.FromArgb(74, 196, 120);       // dot on dark
    public static readonly Color Neutral = Color.FromArgb(108, 122, 137);

    // ---- type
    public const string Family = "Segoe UI";
    public static readonly Font Body = new(Family, 9f);
    public static readonly Font BodySemibold = new("Segoe UI Semibold", 9f);
    public static readonly Font Small = new(Family, 8.25f);
    public static readonly Font SmallSemibold = new("Segoe UI Semibold", 8.25f);
    public static readonly Font SectionTitle = new("Segoe UI Semibold", 9.75f);
    public static readonly Font PageTitle = new("Segoe UI Semibold", 12f);
    public static readonly Font Brand = new("Segoe UI Semibold", 11.25f);
    public static readonly Font Value = new("Segoe UI Semibold", 9.75f);
    public static readonly Font Mono = new("Consolas", 9f);

    // ---- buttons
    public enum ButtonKind { Secondary, Primary, Danger, Chrome }

    public static T Style<T>(T b, ButtonKind kind = ButtonKind.Secondary) where T : ButtonBase
    {
        b.FlatStyle = FlatStyle.Flat;
        b.UseVisualStyleBackColor = false;
        b.Cursor = Cursors.Hand;
        b.Font = kind is ButtonKind.Primary or ButtonKind.Danger ? BodySemibold : Body;
        b.Padding = new Padding(6, 1, 6, 1);
        b.MinimumSize = new Size(0, 28);
        var fa = b.FlatAppearance;
        fa.BorderSize = 1;
        (b.BackColor, b.ForeColor, fa.BorderColor, fa.MouseOverBackColor, fa.MouseDownBackColor) = kind switch
        {
            ButtonKind.Primary => (Accent, Color.White, Accent, AccentHover, AccentHover),
            ButtonKind.Danger => (Danger, Color.White, Danger, DangerHover, DangerHover),
            ButtonKind.Chrome => (ChromeRaised, ChromeText, ChromeBorder, ChromeHover, ChromeHover),
            _ => (Surface, Text, BorderStrong, Hover, Pressed),
        };
        return b;
    }

    /// <summary>Plain caption next to an input.</summary>
    public static Label Caption(string text) => new()
    {
        Text = text, AutoSize = true, ForeColor = TextSecondary, Font = Body,
        Margin = new Padding(0, 7, 6, 3)
    };

    /// <summary>A small all-caps heading inside a section (e.g. "STEP LENGTH").</summary>
    public static Label Overline(string text) => new()
    {
        Text = text.ToUpperInvariant(), AutoSize = true, ForeColor = TextMuted, Font = SmallSemibold,
        Margin = new Padding(0, 8, 0, 2)
    };

    /// <summary>Gives the shell's standard look to inputs anywhere under <paramref name="root"/>.</summary>
    public static void StyleInputs(Control root)
    {
        foreach (Control c in root.Controls)
        {
            switch (c)
            {
                case TextBox t when !t.Multiline:
                case NumericUpDown:
                    c.Font = Body;
                    c.ForeColor = Text;
                    break;
                case ComboBox cb:
                    cb.Font = Body;
                    cb.FlatStyle = FlatStyle.System;
                    break;
                case CheckBox cb when cb.Appearance == Appearance.Normal:
                    cb.ForeColor = Text;
                    break;
            }
            if (c.HasChildren) StyleInputs(c);
        }
    }

    /// <summary>
    /// Makes a label wrap to its width and grow to fit its text. Works on existing labels
    /// (the ones the app writes status into), without changing their type.
    /// </summary>
    public static T Wrapping<T>(T label) where T : Label
    {
        label.AutoSize = false;
        bool busy = false;
        void Fit()
        {
            if (busy || label.Width <= 0) return;
            busy = true;
            try
            {
                int w = Math.Max(1, label.Width - label.Padding.Horizontal);
                var text = label.Text.Length == 0 ? " " : label.Text;
                var size = TextRenderer.MeasureText(text, label.Font, new Size(w, int.MaxValue),
                                                    TextFormatFlags.WordBreak | TextFormatFlags.TextBoxControl);
                int h = size.Height + label.Padding.Vertical + 2;
                if (label.Height != h) label.Height = h;
            }
            finally { busy = false; }
        }
        label.TextChanged += (_, _) => Fit();
        label.SizeChanged += (_, _) => Fit();
        label.FontChanged += (_, _) => Fit();
        return label;
    }

    /// <summary>
    /// Auto-sized buttons and panels default to GrowOnly: after the 125 % / 150 % / 200 %
    /// scaling they would keep the scaled-up size instead of fitting their content again.
    /// </summary>
    public static void FitAutoSize(Control root)
    {
        foreach (Control c in root.Controls)
        {
            if (c.AutoSize)
            {
                if (c is Button b) b.AutoSizeMode = AutoSizeMode.GrowAndShrink;
                else if (c is Panel p) p.AutoSizeMode = AutoSizeMode.GrowAndShrink;   // incl. flow and table panels
            }
            if (c.HasChildren) FitAutoSize(c);
        }
    }

    /// <summary>Scales a 96-dpi pixel value to the control's current DPI.</summary>
    public static int Px(Control c, int px) => (int)Math.Round(px * c.DeviceDpi / 96.0);
}

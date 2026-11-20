using System.Collections;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text.RegularExpressions;
using System.Windows.Forms;
using RoombaMapper;

static void Check(bool condition, string message)
{
    if (!condition) throw new Exception(message);
}

// V3: "--screenshots <folder>" renders the real form at several window sizes instead of testing
if (args.Length >= 2 && args[0] == "--screenshots")
{
    Screenshots.Run(args[1]);
    return;
}

var features = GloveFeatures.Parse("F,heading=buttons,tracker,hand\r\n");
Check(features != null, "feature list should parse");
Check(features!.Contains("tracker"), "tracker must be present");
Check(!features.Contains("stepcal"), "missing feature must remain absent");
Check(GloveFeatures.Parse("F,sim,tracker") == null, "simulation must not gate glove controls");
Check(GloveFeatures.Parse("D,tracker,page") == null, "debug line must not gate glove controls");
using var form = new MainForm();
var flags = BindingFlags.Instance | BindingFlags.NonPublic;
var row = (FlowLayoutPanel)typeof(MainForm).GetField("_gloveRow", flags)!.GetValue(form)!;
var apply = typeof(MainForm).GetMethod("ApplyGloveFeatures", flags)!;
var buttons = row.Controls.OfType<Button>().Where(b => b.Tag is string).ToArray();
Check(buttons.Length == 13, "expected all feature-dependent buttons to be tagged");
apply.Invoke(form, new object?[] { features });
Check(buttons.Where(b => (string)b.Tag! == "stepcal").All(b => !b.Enabled), "missing step calibration must be disabled");
Check(buttons.Where(b => (string)b.Tag! == "tracker").All(b => b.Enabled), "advertised tracker must be enabled");
apply.Invoke(form, new object?[] { null });
Check(buttons.All(b => b.Enabled), "disconnect must reset feature gating");
form.Show();
var debugButton = (Button)typeof(MainForm).GetField("_debugBtn", flags)!.GetValue(form)!;
debugButton.PerformClick();
var window = (Form)typeof(MainForm).GetField("_debugWindow", flags)!.GetValue(form)!;
var onLine = window.GetType().GetMethod("OnLine")!;
onLine.Invoke(window, new object[] { "D,tracker,one|two,three" });
var cards = (FlowLayoutPanel)window.GetType().GetField("_cards", flags)!.GetValue(window)!;
Check(cards.Controls.Count == 1, "a debug page must create a card");
Check(cards.Controls[0].Controls.OfType<Label>().Any(l => l.Text == "one\ntwo,three"), "debug text must render all page fields");
var csvPath = Path.GetTempFileName();
try
{
    var writerField = window.GetType().GetField("_writer", flags)!;
    using (var writer = new StreamWriter(csvPath) { AutoFlush = true })
    {
        writerField.SetValue(window, writer);
        onLine.Invoke(window, new object[] { "D,tracker,say \"hi\",now" });
        writerField.SetValue(window, null);
    }
    Check(File.ReadAllText(csvPath).Contains(",\"tracker\",\"say \"\"hi\"\",now\""), "debug CSV must quote commas and quotes");
}
finally { File.Delete(csvPath); }
window.Close();
Check(!window.IsDisposed && !window.Visible, "closing debug window should hide it");
var source = new TestSource();
var featureLines = new List<string>();
var rawLines = new List<string>();
source.FeatureLine += featureLines.Add;
source.RawLine += rawLines.Add;
source.Feed("F,tracker,hand\nD,tracker,one|two,three\n");
Check(featureLines.SequenceEqual(new[] { "F,tracker,hand", "D,tracker,one|two,three" }), "feature and debug lines must reach the panel");
Check(rawLines.SequenceEqual(new[] { "F,tracker,hand" }), "debug pages must not flood the raw log");
Console.WriteLine("Glove feature parsing, control gating and debug routing tests passed.");

var sim = new RoombaMapper.Sources.FrontierSimulation();
for (int i = 0; i < 20000 && !sim.Done; i++) sim.Step();
Check(sim.Done, "frontier simulation must terminate");
Check(sim.Collisions == 0, "simulated robot must not collide");
Check(sim.Position == sim.WeakestPosition, "simulation must stop at weakest sampled point");
Check(sim.VisitedCount == sim.ReachableFreeCount, "all reachable free cells must be visited");
Console.WriteLine($"Frontier simulation passed: {sim.VisitedCount} cells, {sim.Steps} steps, {sim.Collisions} collisions.");
var walls = new bool[26,18];
for (int y = 0; y < 18; y++) walls[12,y] = true;
walls[12,8] = false;
var maze = new RoombaMapper.Sources.FrontierSimulation(walls, (0,0));
for (int i = 0; i < 20000 && !maze.Done; i++) maze.Step();
Check(maze.Done && maze.Collisions == 0 && maze.VisitedCount == 451 && maze.Position == maze.WeakestPosition, "single-gap room must be fully explored safely");
Console.WriteLine($"Single-gap simulation passed: {maze.VisitedCount} cells, {maze.Steps} steps.");
var simButton = (Button)typeof(MainForm).GetField("_frontierSim", flags)!.GetValue(form)!;
simButton.PerformClick();
Application.DoEvents();
Check(typeof(MainForm).GetField("_active", flags)!.GetValue(form) is RoombaMapper.Sources.FrontierSimulationSource, "simulation button must start the virtual source");
System.Threading.Thread.Sleep(150);
Application.DoEvents();
Check((long)typeof(MainForm).GetField("_telemetrySeq", flags)!.GetValue(form)! > 0, "simulation action must deliver telemetry");
typeof(MainForm).GetMethod("Disconnect", flags)!.Invoke(form, null);
var connectButton = (Button)typeof(MainForm).GetField("_connect", flags)!.GetValue(form)!;
connectButton.PerformClick();
Application.DoEvents();
Check(typeof(MainForm).GetField("_active", flags)!.GetValue(form) is RoombaMapper.Sources.SimulationSource, "normal simulation must remain available");
typeof(MainForm).GetMethod("Disconnect", flags)!.Invoke(form, null);
Console.WriteLine("WinForms frontier simulation action and telemetry passed.");

// ---------------------------------------------------------------- V3 shell
// The controls were moved into a new layout: check they still reach the same code with the
// same command literals, and that the new read-only displays tell the truth.
using var v3 = new MainForm();
v3.Show();
Application.DoEvents();
T Get<T>(string name) => (T)typeof(MainForm).GetField(name, flags)!.GetValue(v3)!;
void Set(string name, object? value) => typeof(MainForm).GetField(name, flags)!.SetValue(v3, value);
object? Call(string name, params object?[] a) => typeof(MainForm).GetMethod(name, flags)!.Invoke(v3, a);
var v3Map = Get<MapView>("_map");
var pages = Get<List<Panel>>("_pages");
var badge = Get<RoombaMapper.Ui.Pill>("_modeBadge");

Check(Get<Button>("_connect").Visible && Get<Button>("_frontierSim").Visible && Get<Button>("_debugBtn").Visible &&
      Get<Button>("_stopMissionBtn").Visible, "connect, frontier simulation, debug and STOP must be visible on the landing page");
Check(v3.Text.Contains("V3"), "the window title must say V3");
Call("UpdateLinkUi");
Check(badge.Text == "NOT CONNECTED", "no source must show NOT CONNECTED");

// Pages: switching shows one page, keeps the one map and never connects
Check(pages.Count == 5, "five pages expected");
for (int i = 0; i < pages.Count; i++)
{
    Call("ShowPage", i);
    Check(pages[i].Visible && pages.Where((p, k) => k != i).All(p => !p.Visible), $"page {i} must be the only one shown");
    Check(ReferenceEquals(Get<MapView>("_map"), v3Map) && v3Map.Visible, "navigation must keep the same visible map");
    Check(Get<object?>("_active") == null, "navigation must not connect");
}

// A source that can send: every robot control must send exactly the original command
var cap = new CaptureSource();
Set("_active", cap);
Call("UpdateControlRow");
Check(Get<Panel>("_controlRow").Enabled, "robot controls unlock when the robot can receive commands");
Check(badge.Text == "REAL HARDWARE", "a hardware source must show REAL HARDWARE");
void Expect(Action act, string command, string what)
{
    cap.Sent.Clear();
    act();
    Check(cap.Sent.Count == 1 && Regex.IsMatch(cap.Sent[0], $"^C,\\d+,{Regex.Escape(command)}$"),
          $"{what} must send {command}; sent: {string.Join(" | ", cap.Sent)}");
}
Call("ShowPage", 1);
foreach (var (field, command) in new[]
{
    ("_runBtn", "RUN"), ("_pauseBtn", "PAUSE"), ("_homeBtn", "HOME"), ("_measureBtn", "MEASURE"), ("_roamBtn", "ROAM"),
    ("_fwdBtn", "STEP,FWD"), ("_backBtn", "STEP,BACK"), ("_leftBtn", "STEP,LEFT"), ("_rightBtn", "STEP,RIGHT"),
})
    Expect(() => Get<Button>(field).PerformClick(), command, field);
Expect(() => Get<ComboBox>("_modeBox").SelectedIndex = 1, "MODE,SLOW", "Wi-Fi mode");
Expect(() => Get<NumericUpDown>("_speedBox").Value = 120, "SPEED,120", "Speed %");
Expect(() => Get<ComboBox>("_sensorBox").SelectedIndex = 1, "SENSORS,FL", "IR sensors");
Check(!Get<CheckBox>("_probeBox").Enabled, "wall probing is only available with the front sensor alone");
Expect(() => Get<ComboBox>("_sensorBox").SelectedIndex = 0, "SENSORS,F", "IR sensors");
Expect(() => Get<CheckBox>("_probeBox").Checked = true, "PROBE,ON", "Probe wall");
Set("_menuPoint", (1234.4, -56.7));
Expect(() => Get<ToolStripMenuItem>("_sendHereItem").PerformClick(), "GOTO,1234,-57", "Send robot here");
Check(v3Map.Target == (1234.4, -56.7), "Send robot here must show the target");
Expect(() => Get<Button>("_stopMissionBtn").PerformClick(), "PAUSE", "STOP");
Call("ShowPage", 3);
Get<TextBox>("_cmdBox").Text = "C,SET,ssid,Lab";
Expect(() => Get<Button>("_cmdSendBtn").PerformClick(), "SET,ssid,Lab", "typed command (C, prefix removed)");
Get<TextBox>("_cmdBox").Text = "SET,pass,hunter2";
Expect(() => Get<Button>("_cmdSendBtn").PerformClick(), "SET,pass,hunter2", "typed password command");
Check(Get<TextBox>("_log").Text.Contains("SET,pass,********") && !Get<TextBox>("_log").Text.Contains("hunter2"),
      "passwords must stay masked in the log");
Call("ShowPage", 2);
var gloveLeft = Get<FlowLayoutPanel>("_gloveRow").Controls.OfType<Button>().First(b => b.Text == "↶ Left 90");
Expect(() => gloveLeft.PerformClick(), "TURN,90", "glove Left 90");

// Navigation must not send anything (pending commands cleared so retries can't be confused with it)
((IDictionary)Get<object>("_pending")).Clear();
cap.Sent.Clear();
for (int i = 0; i < pages.Count; i++) { Call("ShowPage", i); Application.DoEvents(); }
Check(!cap.Sent.Any(l => l.StartsWith("C,")), "switching pages must not send commands");

// Data age: stale data is shown as stale
Set("_lastDataAt", DateTime.Now.AddSeconds(-10));
Call("UpdateLinkUi");
Check(Get<Label>("_linkLbl").Text.Contains("STALE"), "old data must be shown as stale");
Set("_lastDataAt", DateTime.Now);
Call("UpdateLinkUi");
Check(Get<Label>("_linkLbl").Text.Contains("receiving"), "fresh data must be shown as receiving");

// Clear map asks first when there is data, and sends nothing
Call("ShowPage", 0);
v3Map.AddSample(new Telemetry(0, 100, 100, 0, 0, -50));
bool asked = false;
Set("_confirm", new Func<string, string, bool>((_, _) => { asked = true; return false; }));
cap.Sent.Clear();
Get<Button>("_clear").PerformClick();
Check(asked && v3Map.Samples.Count == 1, "cancelling Clear map must keep the data");
Set("_confirm", new Func<string, string, bool>((_, _) => true));
Get<Button>("_clear").PerformClick();
Check(v3Map.Samples.Count == 0 && cap.Sent.Count == 0, "confirmed Clear map clears the display and sends nothing");

// Layer switches drive the real map properties
Get<CheckBox>("_showHeat").Checked = false;
Get<CheckBox>("_showPath").Checked = false;
Check(!v3Map.ShowHeat && !v3Map.ShowPath, "layer switches must change the map");
Get<CheckBox>("_showHeat").Checked = true;
Get<CheckBox>("_showPath").Checked = true;

// Log filter: a filtered view, while the full log keeps every line
Call("AppendLog", "[status] alpha");
Call("AppendLog", "> C,1,RUN");
Call("AppendLog", "P,1,2,3,4,5,-60");
var logKind = Get<ComboBox>("_logKind");
var logView = Get<TextBox>("_logView");
logKind.SelectedIndex = 2;   // commands sent
Check(logView.Visible && logView.Text.Contains("> C,1,RUN") && !logView.Text.Contains("alpha"), "kind filter shows only commands");
logKind.SelectedIndex = 0;
Get<TextBox>("_logSearch").Text = "ALPHA";
Check(logView.Text.Contains("[status] alpha") && !logView.Text.Contains("RUN"), "search is case-insensitive and filters");
Call("AppendLog", "[status] alpha two");
Check(logView.Text.Contains("alpha two"), "new matching lines appear in the filtered view");
Get<TextBox>("_logSearch").Text = "";
var fullLog = Get<TextBox>("_log");
Check(fullLog.Visible && new[] { "alpha", "> C,1,RUN", "P,1,2,3,4,5,-60", "alpha two" }.All(fullLog.Text.Contains),
      "the full log must keep every line");

// Source fields: only the selected source's settings show; USB with no port fails honestly
var sourceBox = Get<ComboBox>("_source");
foreach (var (index, group) in new[] { (1, "_serialGroup"), (2, "_bleGroup"), (3, "_udpGroup") })
{
    sourceBox.SelectedIndex = index;
    Check(Get<FlowLayoutPanel>(group).Visible &&
          new[] { "_serialGroup", "_bleGroup", "_udpGroup" }.Where(g => g != group).All(g => !Get<FlowLayoutPanel>(g).Visible),
          $"source {index} must show only {group}");
}
Set("_active", null);
sourceBox.SelectedIndex = 1;
Get<ComboBox>("_comPort").Items.Clear();
Get<Button>("_connect").PerformClick();
Check(Get<object?>("_active") == null && Get<Label>("_statusLbl").Text.StartsWith("No COM port selected"),
      "USB Serial without a port must say so and not connect");

// Receive-only and simulation sources are labelled honestly
Set("_active", new RoombaMapper.Sources.BleSource("test"));
Call("UpdateControlRow");
Check(!Get<Panel>("_controlRow").Enabled && Get<Label>("_robotHint").Text.Contains("receive-only"),
      "Bluetooth LE must be shown as receive-only");
Set("_active", null);
Get<ComboBox>("_source").SelectedIndex = 0;
Get<Button>("_connect").PerformClick();
Application.DoEvents();
Call("UpdateLinkUi");
Check(badge.Text == "SIMULATION", "the Simulation source must be labelled SIMULATION");
Call("Disconnect");
Call("UpdateLinkUi");
Check(badge.Text == "NOT CONNECTED" && Get<Label>("_missionHint").Text.Contains("Connect"),
      "after disconnecting: NOT CONNECTED and the phase hint says to connect");
Check(!Get<Button>("_phase1Btn").Enabled && !Get<Button>("_phase2Btn").Enabled, "phases need a robot");
Console.WriteLine("V3 shell: navigation, command wiring, gating, labels, clear-map guard and log filter passed.");

// Closing the window while a simulated phase is still stopping must not throw on the UI thread
var uiErrors = new List<Exception>();
Application.ThreadException += (_, e) => uiErrors.Add(e.Exception);
var closing = new MainForm();
closing.Show();
void PumpFor(int ms) { var end = DateTime.Now.AddMilliseconds(ms); while (DateTime.Now < end) { Application.DoEvents(); Thread.Sleep(15); } }
T CGet<T>(string name) => (T)typeof(MainForm).GetField(name, flags)!.GetValue(closing)!;
CGet<ComboBox>("_source").SelectedIndex = 4;                 // Simulated robot (no hardware)
CGet<Button>("_connect").PerformClick();
CGet<NumericUpDown>("_speedBox").Value = 200;
typeof(MainForm).GetMethod("StartPhase1Async", flags)!.Invoke(closing, null);
PumpFor(3000);
Check(typeof(MainForm).GetField("_missionCts", flags)!.GetValue(closing) != null, "phase 1 must be running with the simulated robot");
CGet<Button>("_stopMissionBtn").PerformClick();
closing.Close();
PumpFor(8000);
Check(uiErrors.Count == 0, "closing during a phase threw: " + string.Join("; ", uiErrors.Select(e => e.GetType().Name + ": " + e.Message)));
Console.WriteLine("V3 shell: closing while a simulated phase stops passed.");

/// <summary>Renders the V3 window (offscreen, with DrawToBitmap) for a manual visual review.</summary>
static class Screenshots
{
    private const BindingFlags Flags = BindingFlags.Instance | BindingFlags.NonPublic;
    private static T Field<T>(object o, string name) => (T)o.GetType().GetField(name, Flags)!.GetValue(o)!;

    public static void Run(string dir)
    {
        Application.SetHighDpiMode(HighDpiMode.SystemAware);   // as the app itself runs (ApplicationConfiguration)
        Application.EnableVisualStyles();
        Application.SetCompatibleTextRenderingDefault(false);
        Directory.CreateDirectory(dir);
        Console.WriteLine($"Screen: {Screen.PrimaryScreen!.Bounds.Size}, DPI {Screen.PrimaryScreen.Bounds.Width}, max window {SystemInformation.MaxWindowTrackSize}");

        using var form = new MainForm { StartPosition = FormStartPosition.Manual, Location = new Point(-30000, -30000) };
        form.Show();
        form.WindowState = FormWindowState.Normal;
        void Pump(int ms) { var end = DateTime.Now.AddMilliseconds(ms); while (DateTime.Now < end) { Application.DoEvents(); Thread.Sleep(15); } }
        // Sizes are logical (100 %) pixels; on a high-DPI screen the window is made that many times larger
        double scale = form.DeviceDpi / 96.0;
        void Shot(string name, int w, int h)
        {
            form.Bounds = new Rectangle(-30000, -30000, (int)(w * scale), (int)(h * scale));
            Pump(250);
            using var bmp = new Bitmap(form.Width, form.Height);
            form.DrawToBitmap(bmp, new Rectangle(Point.Empty, bmp.Size));
            string size = $"{Math.Round(form.Width / scale)}x{Math.Round(form.Height / scale)}";
            bmp.Save(Path.Combine(dir, $"{name}_{size}_at{form.DeviceDpi * 100 / 96}pct.png"), System.Drawing.Imaging.ImageFormat.Png);
            Console.WriteLine($"{name}: {size} logical ({form.Width}x{form.Height} px at {form.DeviceDpi} dpi)");
        }
        void Page(int i) { form.GetType().GetMethod("ShowPage", Flags)!.Invoke(form, new object[] { i }); Pump(100); }

        Shot("01_offline", 1366, 768);
        Field<ComboBox>(form, "_source").SelectedIndex = 0;          // Simulation
        Field<Button>(form, "_connect").PerformClick();
        Pump(6000);
        Shot("02_sim_missions", 1366, 768);
        Shot("03_sim_missions", 1920, 1080);
        Shot("04_sim_125pct_of_1080p", 1536, 864);
        Shot("05_sim_150pct_of_1080p", 1280, 720);
        Shot("06_sim_125pct_of_768p", 1093, 614);
        string[] pages = { "missions", "robot", "wearable", "diagnostics", "settings" };
        for (int i = 1; i < pages.Length; i++) { Page(i); Shot($"07_{i}_{pages[i]}", 1366, 768); }
        Page(0);
        Field<TextBox>(form, "_logSearch").Text = "status";
        Shot("08_log_filtered", 1366, 768);
        Field<TextBox>(form, "_logSearch").Text = "";
        form.GetType().GetMethod("Disconnect", Flags)!.Invoke(form, null);
        Pump(300);
        Shot("09_disconnected_after_data", 1366, 768);

        // Mapping phases with the in-app simulated robot. Phase prompts are message boxes:
        // close this process's own dialogs (answering No/Cancel) and record their titles.
        var dialogs = new List<string>();
        using var dismisser = new System.Threading.Timer(_ => DismissOwnDialogs(dialogs), null, 500, 500);
        Field<ComboBox>(form, "_source").SelectedIndex = 4;          // Simulated robot (no hardware)
        Field<Button>(form, "_connect").PerformClick();
        Field<NumericUpDown>(form, "_speedBox").Value = 200;          // fastest animation
        Pump(200);
        bool Running() => form.GetType().GetField("_missionCts", Flags)!.GetValue(form) != null;
        Field<Button>(form, "_phase1Btn").PerformClick();
        var deadline = DateTime.Now.AddSeconds(30);
        Pump(500);
        Shot("10_phase1_running", 1366, 768);
        while (Running() && DateTime.Now < deadline) Pump(200);
        Console.WriteLine($"Phase 1 finished within 30 s: {!Running()}; mission label: {Field<Label>(form, "_missionLbl").Text}");
        Shot("11_phase1_after_30s", 1366, 768);
        Console.WriteLine($"Phase 2 button enabled (needs a room from phase 1): {Field<Button>(form, "_phase2Btn").Enabled}");
        Field<Button>(form, "_stopMissionBtn").PerformClick();
        Pump(1500);
        Console.WriteLine($"1.5 s after STOP: still stopping={Running()}; mission label: {Field<Label>(form, "_missionLbl").Text}");
        Shot("12_after_stop", 1366, 768);
        var stopDeadline = DateTime.Now.AddSeconds(30);
        while (Running() && DateTime.Now < stopDeadline) Pump(200);
        Console.WriteLine($"Phase stopped before closing: {!Running()}");
        Console.WriteLine("Dialogs shown: " + (dialogs.Count == 0 ? "none" : string.Join(" | ", dialogs)));
        form.GetType().GetMethod("Disconnect", Flags)!.Invoke(form, null);
        form.Close();
        Pump(500);
    }

    [DllImport("user32.dll")] private static extern bool EnumWindows(EnumProc cb, IntPtr p);
    [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern int GetClassName(IntPtr h, System.Text.StringBuilder s, int n);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern int GetWindowText(IntPtr h, System.Text.StringBuilder s, int n);
    [DllImport("user32.dll")] private static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] private static extern bool PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
    private delegate bool EnumProc(IntPtr h, IntPtr p);

    private static void DismissOwnDialogs(List<string> seen)
    {
        uint me = (uint)Environment.ProcessId;
        EnumWindows((h, _) =>
        {
            GetWindowThreadProcessId(h, out uint pid);
            var cls = new System.Text.StringBuilder(64);
            GetClassName(h, cls, 64);
            if (pid == me && cls.ToString() == "#32770" && IsWindowVisible(h))
            {
                var title = new System.Text.StringBuilder(256);
                GetWindowText(h, title, 256);
                lock (seen) if (!seen.Contains(title.ToString())) seen.Add(title.ToString());
                const uint WM_COMMAND = 0x111;
                PostMessage(h, WM_COMMAND, (IntPtr)7, IntPtr.Zero);   // IDNO
                PostMessage(h, WM_COMMAND, (IntPtr)2, IntPtr.Zero);   // IDCANCEL (closes OK-only boxes too)
            }
            return true;
        }, IntPtr.Zero);
    }
}

/// <summary>A source that can send, recording every line instead of transmitting it.</summary>
sealed class CaptureSource : RoombaMapper.Sources.DataSourceBase
{
    public readonly List<string> Sent = new();
    public override string Name => "capture";
    public override Task StartAsync() => Task.CompletedTask;
    public override void Stop() { }
    public override bool CanSend => true;
    public override void Send(string line) => Sent.Add(line);
}

sealed class TestSource : RoombaMapper.Sources.DataSourceBase
{
    public override string Name => "test";
    public override Task StartAsync() => Task.CompletedTask;
    public override void Stop() { }
    public void Feed(string text) => OnText(text);
}

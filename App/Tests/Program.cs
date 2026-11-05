using System.Reflection;
using System.Windows.Forms;
using RoombaMapper;

static void Check(bool condition, string message)
{
    if (!condition) throw new Exception(message);
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

sealed class TestSource : RoombaMapper.Sources.DataSourceBase
{
    public override string Name => "test";
    public override Task StartAsync() => Task.CompletedTask;
    public override void Stop() { }
    public void Feed(string text) => OnText(text);
}

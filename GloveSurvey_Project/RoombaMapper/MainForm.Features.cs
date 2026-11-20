namespace RoombaMapper;

/// <summary>
/// The app's feature table: every feature, its switch (Features.cs) and the hooks it
/// wants. The main loop (FeatureRunner, AppFeature.cs) calls the hooks of the switched-on
/// features in this order. To switch a feature off, use Features.cs - not this table.
/// </summary>
public sealed partial class MainForm
{
    private IEnumerable<AppFeature> FeatureTable() => new[]
    {
        // ---- top bar (in this order)
        new AppFeature("Heat map", Features.HeatMap) { OnBuild = BuildHeatToggle },
        new AppFeature("Path", Features.PathLayer) { OnBuild = BuildPathToggle },
        new AppFeature("Export CSV", Features.ExportCsv) { OnBuild = BuildExportCsvButton },
        new AppFeature("Save image", Features.SaveImage) { OnBuild = BuildSaveImageButton },
        new AppFeature("Debug panel", Features.DebugPanel)
        {
            OnBuild = BuildDebugButton, OnFeatureLine = DebugOnFeatureLine, OnDisconnected = DebugOnDisconnected
        },

        // ---- right-hand panel
        new AppFeature("Stats panel", Features.StatsPanel)
        {
            OnBuild = BuildStatsPanel, OnTelemetry = StatsOnTelemetry, OnReading = StatsOnReading, OnClear = StatsClear
        },

        // ---- rows
        new AppFeature("Robot controls", Features.RobotControls) { OnBuild = BuildControlRow, OnTelemetry = ControlsOnTelemetry },
        new AppFeature("Mission row", Features.RobotMission || Features.GloveMode || Features.CommandBox) { OnBuild = BuildMissionRow },
        new AppFeature("Glove mode", Features.GloveMode)
        {
            OnBuild = BuildGloveRow, OnTelemetry = GloveOnTelemetry, OnObstacle = GloveOnHit, OnFeatureLine = GloveOnFeatureLine
        },
    };
}

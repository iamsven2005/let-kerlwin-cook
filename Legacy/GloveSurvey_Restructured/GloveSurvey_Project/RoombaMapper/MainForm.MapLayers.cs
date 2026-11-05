namespace RoombaMapper;

/// <summary>
/// Features "Heat map" and "Path" (Features.HeatMap, Features.PathLayer): their
/// checkboxes in the top bar. The layers themselves are drawn in MapView.Heat.cs and
/// MapView.Raw.cs; an off layer is never drawn.
/// </summary>
public sealed partial class MainForm
{
    private readonly CheckBox _showHeat = new() { Text = "Heat map", Checked = true, AutoSize = true };
    private readonly CheckBox _showPath = new() { Text = "Path", Checked = true, AutoSize = true };

    private void BuildHeatToggle()
    {
        _top.Controls.Add(_showHeat);
        _showHeat.CheckedChanged += (_, _) => _map.ShowHeat = _showHeat.Checked;
    }

    private void BuildPathToggle()
    {
        _top.Controls.Add(_showPath);
        _showPath.CheckedChanged += (_, _) => _map.ShowPath = _showPath.Checked;
    }
}

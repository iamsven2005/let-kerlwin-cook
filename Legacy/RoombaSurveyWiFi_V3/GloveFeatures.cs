namespace RoombaMapper;

public static class GloveFeatures
{
    // Null means this is not a glove capability announcement.
    public static HashSet<string>? Parse(string line)
    {
        if (!line.StartsWith("F,", StringComparison.Ordinal)) return null;
        var features = line.Trim().Split(',').Skip(1).ToHashSet(StringComparer.Ordinal);
        return features.Contains("sim") ? null : features;
    }
}

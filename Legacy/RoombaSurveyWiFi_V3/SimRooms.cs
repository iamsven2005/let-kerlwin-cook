using RoombaMapper.Core;

namespace RoombaMapper;

/// <summary>Virtual rooms for the "Simulated robot (no hardware)" source.</summary>
public static class SimRooms
{
    /// <summary>
    /// 2.1 x 2.4 m room with an alcove at the top right (like the robot sketch's virtual
    /// room), a box in the middle and a small four-legged table. The robot starts at (0,0)
    /// facing +X, near the bottom-left corner.
    /// </summary>
    public static SimWorld Demo()
    {
        var w = new SimWorld();
        w.Floors.Add(new List<(double X, double Y)>
        {
            (-300, -300), (1760, -300), (1760, 2750), (1080, 2750), (1080, 2080), (-300, 2080),
        });
        w.Boxes.Add(SimWorld.Rect(600, 900, 1000, 1250));                 // box
        foreach (var (x, y) in new[] { (150.0, 1450.0), (400.0, 1450.0), (150.0, 1700.0), (400.0, 1700.0) })
            w.Posts.Add((x, y, 20));                                       // table legs
        return w;
    }
}

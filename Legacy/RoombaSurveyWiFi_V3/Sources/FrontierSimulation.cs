namespace RoombaMapper.Sources;

/// <summary>Grid-only simulator. Ground truth is used by virtual sensors and motors, never by routing.</summary>
public sealed class FrontierSimulation
{
    private const int W = 26, H = 18;
    private static readonly (int x, int y)[] Dirs = { (1, 0), (-1, 0), (0, 1), (0, -1) };
    private readonly bool[,] _solid = new bool[W, H];
    // -1 unknown, 0 sensed free, 1 sensed obstacle.
    private readonly int[,] _memory = new int[W, H];
    private readonly bool[,] _visited = new bool[W, H];
    private readonly double?[,] _samples = new double?[W, H];
    private double _weakest = double.PositiveInfinity;
    public (int x, int y) Position { get; private set; } = (1, 16);
    public (int x, int y) WeakestPosition { get; private set; }
    public bool Done { get; private set; }
    public int Steps { get; private set; }
    public int Collisions { get; private set; }
    public int VisitedCount { get; private set; }
    public int ReachableFreeCount { get; }
    public double Rssi => _samples[Position.x, Position.y] ?? Signal(Position.x, Position.y);
    public double HeadingDegrees { get; private set; }
    public string Phase { get; private set; } = "explore";
    public double?[] IrReadings { get; private set; } = new double?[4];

    public FrontierSimulation() : this(null, (1, 16)) { }
    public FrontierSimulation(bool[,]? obstacles, (int x, int y) start)
    {
        if (!Inside(start.x, start.y)) throw new ArgumentOutOfRangeException(nameof(start));
        Position = start;
        for (int x = 0; x < W; x++) for (int y = 0; y < H; y++) _memory[x, y] = -1;
        if (obstacles == null)
        {
            Rect(5,2,8,3); Rect(13,2,15,5); Rect(20,3,23,4); Rect(3,9,6,11);
            Rect(11,8,14,9); Rect(19,10,22,13); Rect(16,14,18,15); Rect(8,14,11,15);
        }
        else
        {
            if (obstacles.GetLength(0) != W || obstacles.GetLength(1) != H) throw new ArgumentException("Expected a 26 by 18 grid.", nameof(obstacles));
            Array.Copy(obstacles, _solid, obstacles.Length);
        }
        if (_solid[start.x, start.y]) throw new ArgumentException("Start is blocked.", nameof(start));
        ReachableFreeCount = CountReachable(); // diagnostic only, never consumed by planner
        Observe();
    }
    private void Rect(int x0, int y0, int x1, int y1)
    { for (int x=x0; x<=x1; x++) for (int y=y0; y<=y1; y++) _solid[x,y]=true; }
    private static bool Inside(int x,int y) => x>=0 && y>=0 && x<W && y<H;
    private int CountReachable()
    {
        var seen = new HashSet<(int,int)> { Position }; var q = new Queue<(int x,int y)>(); q.Enqueue(Position);
        while(q.Count>0) { var p=q.Dequeue(); foreach(var d in Dirs) { var n=(p.x+d.x,p.y+d.y); if(Inside(n.Item1,n.Item2) && !_solid[n.Item1,n.Item2] && seen.Add(n)) q.Enqueue(n); } }
        return seen.Count;
    }
    private double Signal(int x,int y)
    {
        double dx=x-4, dy=y-3; int penalty=0;
        for(int i=1;i<10;i++) { int sx=(int)Math.Round(4+dx*i/10), sy=(int)Math.Round(3+dy*i/10); if(Inside(sx,sy) && _solid[sx,sy]) penalty++; }
        return Math.Max(-110, Math.Round(-28-2.8*Math.Sqrt(dx*dx+dy*dy)-penalty));
    }
    // The four virtual rays are front-left, front-right, left and right. A stationary
    // four-heading scan exposes adjacent cells without a fictitious fifth sensor.
    private void Scan(double heading)
    {
        double[] offsets={-0.44,0.44,-Math.PI/2,Math.PI/2};
        var readings=new double?[4];
        for(int s=0;s<4;s++)
        {
            double a=heading+offsets[s];
            for(double d=0.04;d<=2.65;d+=0.03)
            {
                int x=(int)Math.Floor(Position.x+0.5+Math.Cos(a)*d), y=(int)Math.Floor(Position.y+0.5+Math.Sin(a)*d);
                if(!Inside(x,y)) { readings[s]=d; break; }
                if(_solid[x,y]) { _memory[x,y]=1; readings[s]=d; break; }
                _memory[x,y]=0;
            }
        }
        IrReadings=readings;
    }
    private void Observe()
    {
        var (x,y)=Position; _memory[x,y]=0;
        if(!_visited[x,y]) { _visited[x,y]=true; VisitedCount++; _samples[x,y]=Signal(x,y); }
        if(_samples[x,y]!.Value < _weakest) { _weakest=_samples[x,y]!.Value; WeakestPosition=Position; }
        for(int i=0;i<4;i++) Scan(i*Math.PI/2);
        Scan(HeadingDegrees*Math.PI/180);
    }
    private List<(int x,int y)>? Route(Func<(int x,int y),bool> goal)
    {
        var q=new Queue<(int x,int y)>(); var prev=new Dictionary<(int,int),(int,int)>();
        var seen=new HashSet<(int,int)> { Position }; q.Enqueue(Position);
        while(q.Count>0)
        {
            var p=q.Dequeue();
            if(goal(p)) { var result=new List<(int,int)>(); while(p!=Position) { result.Add(p); p=prev[p]; } result.Reverse(); return result; }
            foreach(var d in Dirs) { var n=(p.x+d.x,p.y+d.y); if(Inside(n.Item1,n.Item2) && _memory[n.Item1,n.Item2]==0 && seen.Add(n)) { prev[n]=p; q.Enqueue(n); } }
        }
        return null;
    }
    public void Step()
    {
        if(Done) return;
        var route = Route(p => !_visited[p.x,p.y]);
        if(route==null)
        {
            Phase="seek weakest";
            route=Route(p => p==WeakestPosition);
            if(route==null) throw new InvalidOperationException("Weakest sampled point is unreachable in the sensed map.");
            if(route.Count==0) { Done=true; Phase="done"; return; }
        }
        var next=route[0];
        HeadingDegrees=Math.Atan2(next.y-Position.y,next.x-Position.x)*180/Math.PI;
        // Virtual motor interlock: this is instrumentation, not planner information.
        if(_solid[next.x,next.y]) { Collisions++; throw new InvalidOperationException("Simulated motor collision"); }
        Position=next; Steps++; Observe();
    }
}

namespace RoombaMapper.Core;

// =====================================================================================
//  PHASE 1.5: REVISIT  (placeholder - not written yet)
//  Goal (spec): finish the incomplete and unresolved wall sections at V_slow, and check
//  whether isolated single hits were accidental (clear), part of a cluster, or part of a
//  solid object. Flagged sections are then shown to the user to draw / mark open / leave.
//  Switched on by Features.RobotPhase15 (off until this is written).
// =====================================================================================
public sealed class Phase15Revisit
{
    private readonly Nav _nav;

    public Phase15Revisit(Nav nav) { _nav = nav; }

    public Task RunAsync(CancellationToken ct)
    {
        _nav.Host.Status("Phase 1.5: not written yet - nothing to do.");
        return Task.CompletedTask;
    }
}

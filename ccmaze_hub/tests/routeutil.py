"""Helpers for route tests: cards from short specs, and pushing chunks straight into a game."""
from ccmaze.sim.cards import Card, CardType, ControlType, shape_family
from ccmaze.sim.game import SeqEntry, Sequence


def mk(spec: str) -> Card:
    """'FOR', 'IF:O:LLU' (condition family O, action LLU), or '<family>:<steps>' e.g. 'O:RDL'."""
    if spec == "FOR":
        return Card(ControlType("FOR"))
    if spec.startswith("IF:"):
        _, fam, steps = spec.split(":")
        return Card(ControlType("IF", fam, CardType(steps, shape_family(steps) or "x")))
    fam, steps = spec.split(":")
    return Card(CardType(steps, fam))


def cards(*specs):
    return [mk(s) for s in specs]


def push_chunk(g, player, specs_or_cards, t):
    """Put the given cards on the table as the player's pending chunk and push it."""
    cs = [mk(s) if isinstance(s, str) else s for s in specs_or_cards]
    pl = g.players[player]
    pl.sequence = Sequence(head="test", entries=[SeqEntry(None, c) for c in cs], opened_at=t)
    assert g.push(player, t)
    return cs


def steps_of(moves):
    return [m.type.steps for m in moves]

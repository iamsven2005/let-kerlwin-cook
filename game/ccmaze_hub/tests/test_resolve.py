"""The required IF / FOR cases from section 14 B2, plus provenance and discard ownership."""
import pytest

from ccmaze.sim.cards import Card, CardType, ControlType
from ccmaze.sim.resolve import resolve


def M(fam, steps):
    return Card(CardType(steps, fam))


def IF(cond, fam, steps):
    return Card(ControlType("IF", cond, CardType(steps, fam)))


def FOR():
    return Card(ControlType("FOR"))


def run(cards):
    return [m.type.steps for m in resolve(cards).moves]


def test_for_repeats_first_card_once_per_card_after_it():
    assert run([FOR(), M("L", "UUR"), M("O", "RDL"), M("S", "RUR")]) == ["UUR", "UUR", "UUR"]


def test_if_matching_family_fires_before_the_card():
    assert run([IF("O", "L", "LLU"), M("O", "RDL")]) == ["LLU", "RDL"]


def test_if_any_orientation_of_family_matches():
    assert run([IF("O", "L", "LLU"), M("O", "DLU")]) == ["LLU", "DLU"]


def test_if_other_family_has_no_effect():
    assert run([IF("O", "L", "LLU"), M("S", "RUR")]) == ["RUR"]


def test_if_at_end_does_nothing():
    assert run([IF("O", "L", "LLU")]) == []


def test_for_expands_before_if():
    assert run([IF("O", "L", "LLU"), FOR(), M("O", "RDL"), M("D", "U")]) == ["LLU", "RDL", "RDL"]


@pytest.mark.parametrize("cond,expected", [("D", ["U", "UU", "R"]), ("O", ["U", "R"])])
def test_for_followed_by_control_card_has_no_effect(cond, expected):
    # D U, FOR, IF(...), D R -> U, then the IF resolves against R
    assert run([M("D", "U"), FOR(), IF(cond, "I3", "UU"), M("D", "R")]) == expected


def test_for_with_one_card():
    assert run([FOR(), M("D", "R")]) == ["R"]


def test_second_for_is_counted_and_consumed():
    assert run([FOR(), M("I3", "UU"), FOR(), M("D", "L")]) == ["UU", "UU", "UU"]


def test_for_alone_and_empty():
    assert run([FOR()]) == []
    assert run([]) == []


def test_provenance_and_ownership():
    cards = [IF("O", "L", "LLU"), FOR(), M("O", "RDL"), M("D", "U")]
    r = resolve(cards)
    assert [(m.kind, m.source) for m in r.moves] == [("if", 0), ("repeat", 2), ("repeat", 2)]
    assert [(m.repeat_i, m.repeat_n) for m in r.moves[1:]] == [(1, 2), (2, 2)]
    assert r.moves[1].via == 1
    assert r.if_effective == [0] and r.for_effective == [(1, 2)]
    assert r.consumed == [3]
    # IF owns move 0; FOR and its C1 own the last repeat; the consumed U owns nothing
    assert r.responsible == {0: 0, 2: 2, 1: 2}
    assert r.idle_cards == [3]

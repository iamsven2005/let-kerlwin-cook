"""Master/slave router: pure logic, no BLE, no hardware.

The master is a BLE central in a star (see PROTOCOL.md). It is the
only router. This module decides, for any incoming message, who
should receive what — and, just as importantly, who should not.

Two directions, deliberately asymmetric:

* Master -> slave(s): the BLE connection IS the address. The caller
  (master.py console, or brain logic) names targets and the
  transport writes the bare payload to each of those connections.
  `resolve_targets()` does group expansion + dedup for that case.

* Slave -> master -> few: the slave has only one connection, so it
  must name its targets in the message:  "R <targets> <payload>".
  `route_from_slave()` parses that, strips the envelope, and
  returns bare-payload deliveries. Anything not starting with "R "
  is master-only and is never forwarded (IR "1", motor "DONE",
  "S0"/"S1", "ID ..." all fall in this bucket).

A "delivery" is a (recipient_role, bare_payload) pair. The transport
(master.py) turns deliveries into BLE writes; the sim turns them
into in-memory inbox appends. Neither is in this file, so the
routing rules are testable without a radio.
"""

# Groups are expanded by the master only. Add your own groups here.
# A group value may name roles and/or other groups; expansion is
# recursive but cycle-safe (a group already being expanded is skipped).
GROUPS = {
    "IR": ["IR-F", "IR-L", "IR-R", "IR-Rear"],
    "SENSORS": ["IR", "WIFI"],
    "ALL": ["*"],  # special: every connected slave, see resolve_targets
}


class Router:
    def __init__(self, groups=None):
        self.groups = dict(GROUPS)
        if groups:
            self.groups.update(groups)
        # role -> True if currently connected. Registering a role that
        # was never seen is how the master learns a board exists; a
        # role with no connection is "known but offline".
        self.known = {}   # role -> bool connected

    # -- registry ---------------------------------------------------------
    def register(self, role):
        self.known[role] = True

    def unregister(self, role):
        if role in self.known:
            self.known[role] = False

    def connected_roles(self):
        return [r for r, on in self.known.items() if on]

    # -- target resolution --------------------------------------------------
    def _expand(self, token, out, seen_groups):
        """Append roles for one token (role, group, or '*') to out."""
        if token == "*":
            out.extend(self.connected_roles())
            return
        if token in self.groups:
            if token in seen_groups:
                return  # group cycle: skip, do not loop forever
            seen_groups.add(token)
            for member in self.groups[token]:
                self._expand(member, out, seen_groups)
            return
        # A plain role: keep it even if unknown/offline. The caller
        # decides whether that is an error (route_from_slave does)
        # or just an empty delivery (resolve for master console does).
        out.append(token)

    def resolve_targets(self, target_spec):
        """'MOTOR' / 'IR-F,IR-L' / 'IR' / 'ALL' -> ordered, deduped roles.

        Unknown/offline roles are kept in the list here; callers that
        need error reporting use route_from_slave / resolve_checked
        instead. Keeping them here lets the master console show
        'no such board' rather than silently dropping a name.
        """
        expanded = []
        for token in target_spec.split(","):
            token = token.strip()
            if token:
                self._expand(token, expanded, set())
        # Deduplicate, preserving first-seen order.
        seen, result = set(), []
        for role in expanded:
            if role not in seen:
                seen.add(role)
                result.append(role)
        return result

    def resolve_checked(self, target_spec, sender=None):
        """Like resolve_targets, plus per-target status.

        Returns (deliverable_roles, problems) where problems is a list
        of (role, 'unknown'|'offline') in first-seen order. The sender
        is always excluded from deliverable_roles, even via ALL/group.
        """
        deliverable, problems = [], []
        for role in self.resolve_targets(target_spec):
            if role == sender:
                continue
            if role not in self.known:
                problems.append((role, "unknown"))
            elif not self.known[role]:
                problems.append((role, "offline"))
            else:
                deliverable.append(role)
        return deliverable, problems

    # -- slave-originated routing -------------------------------------------
    def parse_routed(self, text):
        """Parse 'R <targets> <payload>'.

        Returns (targets, payload) or None if this is not a routed
        message (i.e. it is master-only). A bare 'R' with no targets
        or no payload is also master-only: the master logs it rather
        than guessing at recipients.
        """
        if not text.startswith("R "):
            return None
        rest = text[2:].strip()
        if " " not in rest:
            return None
        targets, payload = rest.split(" ", 1)
        payload = payload.strip()
        if not targets or not payload:
            return None
        return targets, payload

    def route_from_slave(self, sender, text):
        """Route one message received from `sender`.

        Returns a dict:
          kind: 'routed' | 'master_only'
          deliveries: [(role, bare_payload), ...]  (routed only)
          problems: [(role, 'unknown'|'offline'), ...]
          master_text: text  (master_only only; the master should
                              consume/log it, never forward it)
        """
        parsed = self.parse_routed(text)
        if parsed is None:
            return {"kind": "master_only", "master_text": text,
                    "deliveries": [], "problems": []}
        targets, payload = parsed
        deliverable, problems = self.resolve_checked(targets, sender=sender)
        return {"kind": "routed",
                "deliveries": [(role, payload) for role in deliverable],
                "problems": problems}

    # -- master-originated routing ------------------------------------------
    def route_from_master(self, target_spec):
        """Targets for a master console / brain command.

        Returns (deliverable_roles, problems) with the same meaning
        as resolve_checked, but no sender to exclude.
        """
        return self.resolve_checked(target_spec, sender=None)

    # -- accounting -----------------------------------------------------------
    def air_messages(self, sender, target_spec, connected_count):
        """Air messages for a slave-originated message, vs broadcast.

        Slave->master is 1 hop, then 1 per delivered target. Broadcast
        would be 1 + (connected_count - 1). Returns (routed, broadcast).
        """
        deliverable, _ = self.resolve_checked(target_spec, sender=sender)
        routed = 1 + len(deliverable)
        broadcast = connected_count  # 1 in + (n-1) out = n total
        return routed, broadcast

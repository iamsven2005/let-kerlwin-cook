```
=== SPEC QUERY ===
From: Firmware AI
Spec: prompt_node_firmware.md, version 1.0
Status: Brief read in full; TinyScreen, STBLE (BLE TinyShield) and BMA253 references checked. No code written yet. All questions are NON-BLOCKING; I am building with the recommendations below.

Q1 [NON-BLOCKING] [missing]
Section: 1. Hardware
Issue: I can't inspect the fitted BLE shield. The current TinyCircuits BLE TinyShield (ST, ASD2116) uses a BlueNRG-MS (SPBTLE-RF module) with the STBLE library; the older Nordic version (nRF8001) needs a completely different stack.
Options:
  A) ST ASD2116 / BlueNRG-MS, STBLE library (current product)
  B) Nordic nRF8001 shield (discontinued)
Recommendation: A. Please confirm from the label on the shield ("ST" vs "Nordic").
Meanwhile: Building for A. All BLE code is isolated in one file, so B would mean replacing that one file.
Affects other component: no

Q2 [NON-BLOCKING] [missing]
Section: 5. BLE protocol
Issue: The brief says "UART-style service" but gives no UUIDs or write type, and the hub needs both.
Options:
  A) Nordic UART Service, as in TinyCircuits' example: service 6E400001-B5A3-F393-E0A9-E50E24DCCA9E; hub writes to 6E400002-... ; node notifies on 6E400003-... . The RX characteristic accepts both write and write-without-response.
  B) Custom UUIDs
Recommendation: A. It is the de-facto standard, nRF Connect / nRF UART can be used to test it, and hub libraries (e.g. bleak) have examples for it.
Meanwhile: Implementing A. Advertising data carries the name (CCNODE-XXXX); the 128-bit service UUID goes in the scan response because both don't fit in the 31-byte advert. The hub should filter on the name prefix "CCNODE-".
Affects other component: yes

Q3 [NON-BLOCKING] [protocol-change]
Section: 5. BLE protocol (HELLO)
Issue: A notification sent "right after the hub connects" is lost, because the hub hasn't subscribed to TX notifications yet. Separately, the hub gets the F state after HELLO but not the button state.
Options:
  A) Send HELLO + F:<state> when the hub enables notifications (CCCD write) instead of at link-up. The wire format is unchanged; only the trigger moves.
  B) A, plus a new hub->node "HELLO?" that makes the node resend HELLO + state.
  C) A, plus always send B:<hex> after F:<state>, so the hub starts with the full input state.
Recommendation: A + C. A is needed for HELLO to arrive at all. C costs one message and removes a stuck-button edge case.
Meanwhile: A implemented. C not implemented until confirmed (one-line switch, SEND_B_AFTER_HELLO).
Affects other component: yes

Q4 [NON-BLOCKING] [ambiguity]
Section: 5. BLE protocol (framing)
Issue: The brief doesn't say how messages map to GATT writes and notifications.
Options:
  A) Node->hub: exactly one message per notification, always ending in "\n". Hub->node: the node treats the RX bytes as a stream: "\n" ends a message, "\r" is ignored, several messages per write are fine, and a write that ends without "\n" is also treated as one complete message.
  B) Strict stream on both sides; messages may be split across packets.
Recommendation: A. Please confirm the hub sends each hub->node message whole, in a single write.
Meanwhile: A implemented.
Affects other component: yes

Q5 [NON-BLOCKING] [ambiguity]
Section: 5. BLE protocol (REG)
Issue: "REG:<4 bits>, order R L V F": I read it as positional characters, left to right.
Options:
  A) Character 1 = R, 2 = L, 3 = V, 4 = F. So REG:0010 = V set.
  B) A binary number with R as the least-significant bit (rightmost character).
Recommendation: A, matching the "order R L V F" wording.
Meanwhile: A implemented. Characters other than '0'/'1', or a length other than 4, make the node ignore the message.
Affects other component: no (unless the hub means B)

Q6 [NON-BLOCKING] [missing]
Section: 7. Display (card)
Issue: A card that doubles back (e.g. CARD:UD, CARD:LRL) puts two steps on the same cell. Tetris-style blocks can't show the order there.
Options:
  A) Cards never reverse direction (the hub guarantees this).
  B) Reversals are possible. The node then also draws step numbers (1-4) inside the blocks.
Recommendation: A if that's true of the card deck; otherwise B.
Meanwhile: Each straight run gets its own colour, the last block gets an arrow, and the start cell is outlined. A revisited cell shows the later step. Step numbers can be turned on with CARD_STEP_NUMBERS.
Affects other component: no

Q7 [NON-BLOCKING] [missing]
Section: 5 / 7 (defaults, reconnect)
Issue: The brief doesn't give display defaults or say what happens to hub-supplied state (card, SEQ, REG, COL, HAND, MSG, DISP) when the link drops and comes back.
Options:
  A) On disconnect, reset everything to defaults: no card, no badge, REG 0000, grey colour, no hand, no MSG, DISP:0 (display-off allowed). The hub resends full state after every HELLO.
  B) Keep the last state across a reconnect.
Recommendation: A. A stale card after a reconnect is worse than a blank one.
Meanwhile: A implemented.
Affects other component: yes (the hub must resend state after each HELLO)

Q8 [NON-BLOCKING] [ambiguity]
Section: 5. BLE protocol (MSG / BLINK)
Issue: The brief doesn't say how MSG interacts with other messages.
Options:
  A) While a MSG is shown, SEQ/REG/COL/HAND update the stored state silently; the MSG stays until the next CARD:. An empty "MSG:" clears the message. BLINK flashes, then returns to whatever was shown.
  B) Any display message clears the MSG.
Recommendation: A.
Meanwhile: A implemented.
Affects other component: no

Q9 [NON-BLOCKING] [ambiguity]
Section: 3. Tap detection (suppression around a flip)
Issue: Picking a node up causes spikes before zAvg starts to move: the 0.02 filter has a ~50 ms time constant, and the lift comes before the tilt. Suppressing only "from the moment zAvg starts crossing" therefore can't stop a pickup from producing a stray T:V. Looking back in time would require delaying every tap.
Options:
  A) As written: suppress from the zAvg crossing forward. Accept an occasional stray T:V on pickup.
  B) Hold every tap ~60 ms before sending and cancel it if a tilt starts. This adds latency and breaks "send immediately".
  C) A, plus a second, faster tilt detector (≈10 ms filter) that starts the suppression window earlier.
Recommendation: C. If stray taps still happen, the hub can drop a T:V that is followed by an F: commit within ~1 s.
Meanwhile: C implemented (FAST_TILT_ALPHA; set it to 0 to get plain A).
Affects other component: no (yes only if the hub adds the optional filter)

Q10 [NON-BLOCKING] [ambiguity]
Section: 3. Tap detection (R/L when face-down)
Issue: When a node is flipped over its long axis, its own right side ends up on the players' left. The brief doesn't say whether R/L is the node's own side or the side as seen from the table.
Options:
  A) Device frame: R = the node's own right edge (relative to its screen), whether face-up or face-down. The hub knows F and can remap.
  B) World frame: the node swaps R/L while face-down. This is only right if it is always flipped about the same axis, which the node can't know.
Recommendation: A.
Meanwhile: A implemented. Spikes dominated by Y (knocks on the top/bottom edge) are ignored, and no event is sent.
Affects other component: yes (the hub must know which frame it gets)

Q11 [NON-BLOCKING] [missing]
Section: 5. BLE protocol (latency)
Issue: The node will request a 7.5-15 ms connection interval after connecting, but the central decides. macOS usually won't go below 15 ms, and Linux/BlueZ often keeps 30-50 ms unless configured on the hub side.
Options:
  A) Node requests 7.5-15 ms; the hub side also configures or requests short intervals where its OS allows it.
  B) Node-side request only.
Recommendation: A. Which OS does the hub run on? I'll add the matching hub-side notes to the README.
Meanwhile: The node requests 6-12 units (7.5-15 ms), slave latency 0, supervision timeout 4 s, ~1 s after connecting. The negotiated interval is printed in debug mode.
Affects other component: yes

Q12 [NON-BLOCKING] [protocol-change]
Section: 5. BLE protocol (proposed addition)
Issue: Up to six battery nodes run at once, and nothing tells the hub when one is about to die mid-game.
Options:
  A) Add node->hub "BAT:<percent>" (or "BAT:<mV>"), sent after HELLO and every 60 s.
  B) No battery reporting.
Recommendation: A. It's cheap, and the hub can warn players between rounds.
Meanwhile: Not implemented (protocol unchanged until confirmed).
Affects other component: yes
=== END SPEC QUERY ===
```

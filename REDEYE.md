### Atari Lynx Redeye Communications ###

## Normal Logon and Game-Phase Communications Flow

# 1. Overview

Redeye is the Atari Lynx's multiplayer communication protocol for coordinating multiple Lynx systems over the ComLynx serial connection.

The protocol has two distinct phases:

1. **Logon phase**

   * Discover the other Lynx systems.
   * Assign unique player numbers.
   * Determine which players are participating.
   * Detect conflicting or inconsistent player assignments.
   * Reach agreement that all participating Lynxes have the same player set.
   * Terminate logon.
   * Compact player numbers when players disappear.

2. **Normal game phase**

   * Player 0 becomes the master.
   * Game data is exchanged in a deterministic sequence.
   * Each player contributes one data message per sequence.
   * Missing data can be explicitly requested.
   * The master can request specific player data and retransmit data when necessary.
   * The sequence alternates between two sequence values.

The implementation in `redeye.s` uses a combination of the Lynx UART, hardware timers, interrupt-driven message transmission/reception, and higher-level Redeye state machines.

---

# 2. Physical Serial Characteristics

The normal Redeye configuration uses 62,500 baud. The logon phase always uses this speed, but game data can be sent at slower speeds.

The source explicitly documents the supported Redeye speeds:

* `REDEYE_SLOWNESS = 0`: 62,500 baud
* `REDEYE_SLOWNESS = 1`: 31,250 baud
* `REDEYE_SLOWNESS = 2`: 15,625 baud
* `REDEYE_SLOWNESS = 3`: 7,812.5 baud

At 62,500 baud, one serial bit is approximately:

**16 µs**

With the UART's 11-bit character timing, one byte occupies approximately:

**176 µs**

The Redeye source uses this value directly in its TX-to-RX turnaround timing.

The serial connection is effectively half-duplex. The Lynx receives its own transmitted data, and the message manager uses that behavior to detect whether another Lynx transmitted simultaneously. After transmission, the last received byte is compared with the last byte transmitted. A mismatch indicates a collision/transmission error.

---

# 3. Redeye Message Format

Every Redeye message is handled by the Lynx message manager.

The message manager adds:

* a length byte
* the Redeye message/data bytes
* a checksum byte

The Redeye code constructs the message contents in `XmitBuffer` and then calls `sendLengthMessage`. The message manager calculates the checksum before transmission.

The Redeye protocol uses the low three bits of the first Redeye byte to distinguish normal message classes.

The normal game-phase handlers are:

| Type | Purpose           |
| ---- | ----------------- |
| 3    | Player data       |
| 4    | Send-data request |
| 5    | Resend request    |

The first byte also contains the player number and sequence information.

Conceptually, the first byte contains:

bit 7       sequence
bits 6-3    player number
bits 2-0    message type

---

# 4. Logon Phase

## 4.1 Starting Logon

When logon begins, the Redeye variables are initialized. Initially a lynx becomes player number 0.

Player 0 is immediately placed into the active-player mask.

The inconsistency counter is initialized, and `NumberOfPlayers` starts at zero.

The logon system also initializes the message manager and starts the logon timer.

The logon timer uses the Lynx timer with a 64 µs divider.

---

# 5. Logon Message Type 0

The normal logon message announces a player's current identity and view of the multiplayer session.

The message contains:

message type
player number
active-player mask
game ID

The source defines:
LOGON_MSG_SIZE = 4 + MASK_SIZE

For a two to eight player configuration, `MASK_SIZE` is one byte, so the Redeye payload is five bytes.

The game ID is included so that Lynxes configured for different games do not accidentally join the same Redeye session. A received message whose game ID does not match the local `GAME_ID` is rejected.

A received logon message is therefore validated for:

1. message status/errors
2. correct message length
3. correct game ID
4. valid message type
5. valid player number

Only after those checks does Redeye process the logon information.

---

# 6. Logon Timing

Logon deliberately does not operate as a fixed rapid round-robin.

Instead, the machines use randomized delays to avoid continually colliding with one another.

The source documentation describes the algorithm as follows:

* If a Lynx hears the player whose number cyclically precedes it, it schedules itself to speak after approximately **3–12 ms**.
* For other events, it schedules itself after approximately **16–32 ms**.
* When everything is stable, the machines therefore speak their player numbers in rapid cyclic succession.

The actual random delay is generated from timer-derived random data. The source specifically masks the random value to create the 16–32 ms delay range.

---

# 7. Player Heard Timers

Redeye maintains a `PlayerHeard[]` value for every possible player number.

When a player is heard, its counter is reset to: 40

This is defined as:
tolerance_of_silent_player = 40

The counter is decremented after logon transmissions.

If a player's counter reaches zero, Redeye assumes that player has either:

* disappeared, or
* changed player number.

The player is then removed from the active-player mask.

Removing a player also cancels a pending end-of-logon request and sets the inconsistency counter.

This is important because Redeye does not immediately assume that a stable-looking player list is final. It requires continued observation of the participants.

---

# 8. Active Player Mask

Each Lynx maintains an `ActvPlrMask`.

This represents the player's current understanding of which player numbers are active.

For example, with four players:

Player 0  -> bit 0
Player 1  -> bit 1
Player 2  -> bit 2
Player 3  -> bit 3

A logon message carries this mask.

When a Lynx receives another player's logon message, it compares the received active-player mask with its own.

If they disagree, the local `inconsistancy` counter is reset to its preset value.

The inconsistency counter is therefore a stabilization mechanism.

Redeye does not terminate logon merely because one machine happens to believe that the player list is correct. The machines must continue exchanging information until the inconsistency condition has aged out.

The source explicitly states that logon can terminate only when:

1. the inconsistency counter has reached zero, and
2. all players have been heard from recently.

---

# 9. Player Number Collision Detection

Player numbers are dynamically assigned during logon.

If a Lynx hears another Lynx announce the same player number, it recognizes the collision.

The relevant logic is:

cmp PlayerNumber
bne ...
jsr pick_new_player_number

The Lynx then searches `PlayerHeard[]` for an unused player number.

If one is available, it assigns itself that number and forces the corresponding player bit active.

The algorithm is intentionally decentralized. Each Lynx maintains its own view of which numbers are occupied.

This means a collision does not require a central authority.

---

# 10. What Happens When a Lynx Has No Valid Player Number

If a Lynx cannot use its current player number, it enters the "too many players / no player number" state.

The source sets:

PlayerNumber = 0
NumberOfPlayers = 0
logon_state = 2

It then starts another randomized **16–32 ms** timer.

When that timer expires, the Lynx decrements the player-heard counters, searches for an available player number, and tries again.

If another Lynx's logon message is received while the local machine has no player number, the receiving machine can use the received player information to help force an active player and choose a new number.

---

# 11. Normal Stable Logon Cycle

Once the players have unique numbers and their views are becoming consistent, logon settles into a repeating cycle.

Conceptually:

Player 0 announces itself
        ?
Player 1 announces itself
        ?
Player 2 announces itself
        ?
...
        ?
highest active player
        ?
Player 0
        ?
repeat

The exact delay depends on whether the next speaker is the cyclic successor and whether there is already a gap on the bus.

The source uses approximately 3–7 ms when there is no intervening gap, and 8–12 ms when an additional gap is appropriate.

If it is not the local Lynx's turn, it uses the longer 16–32 ms delay.

---

# 12. Requesting the End of Logon

The user can request that logon terminate.

In the glue code, pressing the appropriate logon buttons sets:

EndLogonRequest

The actual protocol does not immediately terminate.

Instead, the request is remembered and Redeye waits until the participant set has stabilized.

During each normal logon transmission, Redeye:

1. decrements the inconsistency counter,
2. calculates the number of active players,
3. checks whether inconsistency has reached zero,
4. checks whether an end-logon request exists.

Only when the conditions are satisfied does the termination sequence begin.

---

# 13. Beginning Logon Termination

When logon has stabilized and termination has been requested, the master enters:

logon_state = 4

and prepares an end-logon message.

The source loads:

XmitBuffer+1 = 10

and changes the state to the terminating state.

The significance of the value 10 is that it is used as the countdown value passed to the other Lynxes.

---

# 14. End-Logon / Countdown Message

The end-logon message is handled by `end_logon_command`.

The message contains a countdown value in the byte following the command.

When the command is processed, Redeye:

1. stops the current logon timer,
2. loads `RxMsg+1` into `timer_high_order_countdown`,
3. enters `logon_state = 6`,
4. starts the timer again.

The relevant source is:

lda #TIMER_DONE
sta TIM5CTLB

lda RxMsg+1
sta timer_high_order_countdown

finish_logon_next_timeout:
    lda #6
    sta logon_state

wait_16_milliseconds:
    lda #255
    sta TIM5CNT
    stz TIM5CTLB

This is important: **receiving another end-logon command while already in countdown state reloads the countdown.**

For example:

current countdown = 5
receive end-logon message with countdown = 9

The value becomes:
countdown = 9

and the timer is restarted.

Therefore the countdown is not simply a one-time timer started by the first message. A subsequent valid end-logon command can extend/reinitialize the countdown.

---

# 15. Countdown Timer Mechanics

The logon timer uses:
AUD_64

and operates as a one-shot timer.

The high-order countdown value is decremented every time the low-order timer expires.

While the high-order count is nonzero, the low-order timer is reloaded.

The source explicitly comments:

; timer runs for count+1 clocks

When the high-order countdown finally reaches zero, the timer dispatches according to `logon_state`.

For state 6, execution reaches:

end_logon_right_now

which terminates the logon system.

---

# 16. Why the End-Logon Countdown Exists

The countdown gives all participating Lynxes time to receive the termination command and transition out of the discovery phase.

The important sequence is therefore:

stable logon
      ?
end-logon request
      ?
master sends termination/countdown message
      ?
other Lynxes receive countdown
      ?
all enter countdown state
      ?
countdown expires
      ?
logon terminates
      ?
player numbers are finalized/compacted
      ?
normal Redeye begins

The master may transmit the end-logon command and then wait approximately one 16 ms timer interval before continuing the termination process.

---

# 17. Player Number Compaction

At the end of logon, Redeye compacts the player numbering.

The final termination routine starts with the local player number and walks downward through the possible player numbers.

If it finds a missing player slot, it decrements the local player number.

Conceptually:

Before compaction:

Player 0 = active
Player 1 = missing
Player 2 = active

After compaction:

Player 0 = active
Player 1 = active

The relevant code is:

ldx PlayerNumber

@00:
    lda PlayerHeard,x
    bne @01
    dec PlayerNumber

@01:
    dex
    bpl @00

Then Redeye disconnects the logon communication system.

This is why player numbers entering the normal game phase are contiguous, beginning with player 0.

---

# 18. Transition to Normal Redeye

After logon terminates, Redeye shuts down the logon-specific communication state and starts the normal communication system.

start_comlink initializes the normal Redeye message manager.

The normal communication phase uses:

PlayerNumber
NumberOfPlayers
Sequence
RxMask
PlayerFlag0
PlayerFlag1
WhosNext

The two player-data buffers correspond to the two alternating sequence values:

PlayerData0 = sequence 0
PlayerData1 = sequence 1

---

# 19. Player 0 Becomes Master

Player 0 is the master.

During normal Redeye initialization, only player 0 installs:

TimeoutInt
ChkResendQueue

as the normal inter-message timeout and message-sent handlers.

Player 0 also sets its `OutGoingFlag` so that it is permitted to send the first data message.

The other players do not operate as independent masters.

---

# 20. Starting a Normal Data Sequence

When a player sends its data, Redeye toggles its sequence value.

The sequence is represented by bit 7:

sequence 0
sequence 1
sequence 0
sequence 1
...

After sending data, the sender constructs a new `RxMask` representing the players from whom it needs data for the new sequence.

The code then initializes:

WhosNext = 1

for the master.

Thus a normal sequence is effectively:

Player 0
   ?
Player 1
   ?
Player 2
   ?
...
   ?
last player

followed by the next sequence.

---

# 21. Player Data Message — Type 3

A normal player data message has type 3.

The first byte identifies:

* the sending player,
* the sequence,
* message type 3.

The remainder of the message contains the player's application/game data.

For a fixed-size build, the source uses:

PLAYER_DATA_SIZE

bytes of player data.

The default source example uses four bytes.

The receiver verifies:

1. message size,
2. player number,
3. sequence,
4. whether the message has already been received.

If valid, the data is placed into either:

PlayerData0
or:
PlayerData1

depending on sequence.

---

# 22. RxMask — Tracking Missing Players

After sending its own data, a Lynx constructs `RxMask`.

`RxMask` represents the players whose data has not yet been received for the current sequence.

For example, conceptually:

RxMask:

bit 0 = waiting for Player 0
bit 1 = waiting for Player 1
bit 2 = waiting for Player 2
...

When a valid data message arrives, that player's bit is cleared.

The source performs this operation when accepting a new player-data message.

When all required data has been received, the next turn can proceed.

---

# 23. Normal Turn Advancement

For non-master players, receiving the preceding player's data is what normally causes the local player to send its own data.

The source checks whether:

received player + 1 == my PlayerNumber

and, when appropriate, calls:

TryToSendMyData

Thus normal Redeye is fundamentally a **round-robin protocol**.

Each player waits for the preceding player's contribution before transmitting its own.

---

# 24. Send Requests — Type 4

Player 0 has an additional responsibility.

If it is waiting for a particular player's data, it can explicitly request that player to send.

`WhosNext` identifies the player currently expected to provide data.

`AskForSend` constructs a type-4 message addressed to that player.

The source does:

lda WhosNext
...
asl a
asl a
asl a
ora #4
ora Sequence
eor #%10000000
sta XmitBuffer

and sets:
LongTimeoutFlag = 1

The request is therefore associated with both:

* a player number
* a sequence

---

# 25. Long Timeout After a Type-4 Request

Once the type-4 request has actually finished transmitting, `ChkResendQueue` sees `LongTimeoutFlag` and starts the long timeout.

The source defines:

Long_Divider = AUD_64
Long_TIMEOUT = 16000/64

The timer is therefore configured for approximately:

**16 ms**

before the master concludes that the requested response has not arrived.
This is distinct from the **16–32 ms randomized logon delay**.

They are separate timers serving different purposes.

---

# 26. Repeated Send Requests

If the requested player does not provide its data before the long timeout expires, the master proceeds through:

TimeoutInt
    ?
ChkResendQueueAndSend
    ?
FigureAndAskForSend
    ?
FigureWhosNext
    ?
AskForSend

FigureWhosNext searches the outstanding `RxMask` beginning with the current `WhosNext`.

If that player is still missing, it remains the requested player.

Consequently, if Player 2 is still outstanding, the master can repeatedly produce:

Type 4 ? Player 2
       ?
~16 ms
       ?
Type 4 ? Player 2
       ?
~16 ms
       ?
Type 4 ? Player 2


The request moves on only when the outstanding-data state indicates that the current player is no longer missing.

---

# 27. Type-5 Resend Request

Type 5 is a resend request.

Unlike type 4, which asks a player to send its current data, a type-5 request tells the master that specific player data needs to be retransmitted.

A resend request contains:

* requesting player
* sequence
* mask identifying the players whose data should be retransmitted

The source constructs this using `AskForResend`.

The master receives the request in `RcvResendReq`.

It first calls:

FigureWhosNext

and verifies that the requested player corresponds to the player it currently expects.

If the request is valid, the master records:

WhosReTxReq
ReTxSequence
ReTxMask

and enters the retransmission process.

---

# 28. Retransmission

`ResendNext` walks through the retransmission mask.

For every requested player, it calls:

SendPlrXSequenceData

using the appropriate sequence.

After the retransmission mask is exhausted, Redeye returns to the normal:

FigureWhosNext
AskForSend

process.

Thus retransmission is integrated into the normal round-robin protocol rather than being a separate communication mode.

---

# 29. Duplicate Data

The receiver tracks whether it has already received a particular player's data for a particular sequence.

There are separate flags for:

PlayerFlag0
PlayerFlag1

When a new valid message arrives, Redeye checks the appropriate flag.

If the data has already been received, it does not overwrite the normal receipt state as though it were new data.

This allows retransmitted data to be recognized without corrupting normal sequence progression.

---

# 30. Message Timing During Normal Game Operation

The normal game phase uses several distinct timing mechanisms.

They should not be confused with each other.

### 30.1 UART character timing

At 62,500 baud:

~176 µs per character

The TX-to-RX turnaround timer is based on:

TxToRx_TIMEOUT = 176-60

The receiver is therefore re-enabled shortly after the final transmitted character has completed.

---

### 30.2 Message-gap timeout

The message-gap timer uses:

MSG_GAP_TIMEOUT = 380/4

with an AUD_4 divider.

Its purpose is to detect a gap between bytes large enough to indicate that the current message has ended or that the next byte represents a new message.

Every received byte restarts this timer.

---

### 30.3 Master inter-message timeout

The normal inter-message timeout is:

InterMsg_TIMEOUT = 1000/4


with an `AUD_4` divider.

This corresponds to approximately:

**1 ms**

The timer is primarily used by player 0.

The source explicitly prevents non-master players from starting this timeout:

lda PlayerNumber
bne startInterMsgTimeout1


This is an important characteristic of Redeye: the master has the responsibility for advancing the communication cycle when the normal round-robin progression does not happen quickly enough.

---

### 30.4 Long timeout

The long timeout is:

Long_TIMEOUT = 16000/64


using an `AUD_64` divider.

This is approximately:

**16 ms**

It is used after the master sends a type-4 request for player data.

---

# 31. Transmission Collision Detection

The Lynx UART receives what it transmits.

At the end of a transmission, Redeye waits for the TX-to-RX turnaround interval and then compares:

last received byte

against:

last transmitted byte


If they do not match, Redeye considers the transmission to have collided or otherwise failed.

The message manager invokes the transmission-error handler in this case.

This mechanism is particularly important during logon because multiple Lynxes may independently decide that it is time to transmit.

The randomized logon delays reduce the probability of repeated collisions.

---

# 32. Message Reception and Integrity

Every incoming message passes through the message manager.

The receiver tracks:

* message length
* byte count
* checksum
* UART parity errors
* overrun errors
* framing errors
* message-gap timeout

At the end of a message, the checksum is evaluated.

If the checksum is invalid, MSG_CHECKSUM_ERR is recorded and the higher-level Redeye code can reject the message.

---

# 33. What Happens When a Message Is Too Slow

The message manager distinguishes between two different timeout conditions.

### Incomplete message

If bytes have started arriving but the next byte fails to arrive within the message-gap interval:

text
MsgGapTimeout


is generated.

The partial message is abandoned and the receiver is prepared for another message.

### No new message

If no message begins within the inter-message timeout:

text
InterMsgTimeout


is generated.

For the master, this causes the Redeye protocol to advance its communication state.

---

# 34. Complete Normal Communication Cycle

A simplified normal game cycle therefore looks like this:

text
                    +----------------------+
                    ¦  Player 0 sends data ¦
                    +----------------------+
                               ?
                    Player 1 sends data
                               ?
                    Player 2 sends data
                               ?
                           ...
                               ?
                    Last player sends data
                               ?
                  All players have sequence N
                               ?
                       sequence toggles
                               ?
                    Player 0 sends next data
                               ?
                           repeat


When a player fails to send:

text
Player 0
   ¦
   +-- type 4 ? missing player
   ¦
   +-- wait ~16 ms
   ¦
   +-- if still missing ? request again
   ¦
   +-- continue when required data arrives


If a player determines that it needs previously transmitted data:

text
Player
   ¦
   +-- type 5 resend request ? master
                                  ¦
                                  ?
                         retransmit requested
                         player/sequence data


---

# 35. Sequence Alternation

Redeye maintains two sets of player-data storage:

PlayerData0
PlayerData1


and two sets of receipt flags:

PlayerFlag0
PlayerFlag1


The sequence bit alternates after each player's own data transmission.

Conceptually:

Sequence 0
    Player 0
    Player 1
    Player 2
    ...

Sequence 1
    Player 0
    Player 1
    Player 2
    ...

Sequence 0
    Player 0
    Player 1
    Player 2
    ...


This allows Redeye to retain data from the previous sequence while the next sequence is being assembled.

---

# 36. Game-Level Synchronization

The Redeye communication layer itself does not interpret the meaning of the player data.

It provides the game with the collected data for each player.

In the supplied glue code, for example, the player data contains joystick and switch information.

The game waits until the required data flags are available and then copies each player's data into the game-facing arrays.

This creates the fundamental synchronization model:

collect everyone's input
        ?
make the complete input set available
        ?
game processes the frame
        ?
collect next input set
        ?
repeat


---

# 37. Complete Protocol Timeline

The complete normal Redeye lifecycle can be summarized as follows.

POWER-UP / RED EYE START
        ¦
        ?
Initialize communication
        ¦
        ?
Become Player 0 initially
        ¦
        ?
Send normal logon messages
        ¦
        +---------------+
        ¦               ¦
        ?               ¦
Hear other players      ¦
        ¦               ¦
        ?               ¦
Update PlayerHeard[]    ¦
        ¦               ¦
        ?               ¦
Compare active masks    ¦
        ¦               ¦
        +-- mismatch ---+
        ¦
        ?
Resolve player collisions
        ¦
        ?
Assign unused player numbers
        ¦
        ?
Continue randomized logon exchanges
        ¦
        ?
All players heard recently
AND
inconsistency counter = 0
        ¦
        ?
End-logon request
        ¦
        ?
Master enters termination state
        ¦
        ?
Send end-logon/countdown message
        ¦
        ?
Other players load countdown
        ¦
        ?
Countdown timer
        ¦
        ¦   Additional end-logon message
        ¦   can reload/extend countdown
        ¦
        ?
Countdown expires
        ¦
        ?
Compact player numbers
        ¦
        ?
End logon
        ¦
        ?
Initialize normal Redeye
        ¦
        ?
Player 0 = master
        ¦
        ?
Initialize RxMask / sequence state
        ¦
        ?
Player 0 sends first game data
        ¦
        ?
Player 1 sends
        ¦
        ?
Player 2 sends
        ¦
        ?
...
        ¦
        ?
All required player data received
        ¦
        ?
Sequence toggles
        ¦
        ?
Next round
        ¦
        +-- missing player?
        ¦       ¦
        ¦       +-- type 4 request
        ¦               ¦
        ¦               +-- ~16 ms timeout
        ¦
        +-- missing older data?
        ¦       ¦
        ¦       +-- type 5 resend request
        ¦
        +-----------------------+
                                ¦
                                ?
                         Continue game


---

# 38. Timing Summary

| Event                               |                                                   Nominal timing |
| ----------------------------------- | ---------------------------------------------------------------: |
| Serial bit at 62,500 baud           |                                                           ~16 µs |
| Serial character                    |                                                          ~176 µs |
| TX ? RX turnaround                  |                                                          ~116 µs |
| Message-gap timer                   |                                                          ~380 µs |
| Master normal inter-message timeout |                                                            ~1 ms |
| Logon "my turn" delay               |                                                          ~3–7 ms |
| Logon delayed turn                  |                                                         ~8–12 ms |
| General logon random delay          |                                                        ~16–32 ms |
| Normal type-4 response timeout      |                                                           ~16 ms |
| Logon player-heard tolerance        |                                            40 logon transactions |
| End-logon countdown                 | Controlled by countdown value; implemented using the logon timer |

The **16–32 ms value is a logon randomization delay**. The **~16 ms long timeout is a normal-game type-4 response timeout**. They are separate mechanisms.

---

# 39. Important Conceptual Distinctions

Several parts of Redeye are easy to confuse.

### Logon is not normal game communication

Logon is decentralized and randomized.

Normal Redeye is deterministic and round-robin.

### Player discovery is not the same as player synchronization

During logon, PlayerHeard[], ActvPlrMask, collisions, and the inconsistency counter establish the participant list.

During the game, RxMask, sequence flags, and WhosNext establish which game data is outstanding.

### Type 4 is not a resend

Type 4 means:

text
"Player X, send your current requested data."


Type 5 is the resend mechanism.

### The master is special

Player 0 is responsible for advancing the protocol when the normal round-robin progression does not occur.

This is why the normal inter-message timeout and resend queue are installed specifically for player 0.

### The sequence bit is not a player number

The player number identifies who produced the data.

The sequence bit identifies which alternating data set the message belongs to.

### Logon countdown is not a normal-game timeout

The logon countdown uses Timer 5 and the logon state machine.

The normal-game long timeout uses Timer 1 and is approximately 16 ms.

---

# 40. Final Protocol Model

At the highest level, Redeye can be understood as two state machines.

## Logon

DISCOVER
   ?
ASSIGN PLAYER NUMBERS
   ?
DETECT COLLISIONS
   ?
COMPARE ACTIVE MASKS
   ?
WAIT FOR STABILITY
   ?
END-LOGON COUNTDOWN
   ?
COMPACT PLAYER NUMBERS
   ?
START GAME


Its primary concerns are:

* Who is present?
* What player number does each machine have?
* Do all machines agree on the participant set?
* Has the participant set remained stable long enough?
* When can logon safely terminate?

## Game

SEND PLAYER DATA
        ?
COLLECT PLAYER DATA
        ?
WAIT FOR NEXT PLAYER
        ?
REQUEST MISSING DATA IF NECESSARY
        ?
RETRANSMIT IF REQUESTED
        ?
COMPLETE SEQUENCE
        ?
TOGGLE SEQUENCE
        ?
REPEAT


Its primary concerns are:

* Which player's data is next?
* Which player data is still missing?
* Which sequence is being collected?
* Does the master need to request data?
* Does a player need data retransmitted?

The original Redeye design therefore uses **randomized collision avoidance during logon**, followed by a **deterministic token/round-robin style exchange during the game**. The serial message manager supplies the byte-level timing, collision detection, checksumming, and timeout machinery underneath both phases.

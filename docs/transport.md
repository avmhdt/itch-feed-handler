# Transport

`itch replay` publishes a file as MoldUDP64 over local UDP multicast, and `itch listen` receives it,
recovers any loss, and drives the same parser and books as file mode. Section names refer to the
MoldUDP64 1.00 specification.

Traffic stays on loopback: the sender uses TTL 0 and the loopback interface, and `UdpSocket` refuses
any interface outside 127.0.0.0/8.

## Publisher

ITCH files and MoldUDP64 message blocks have the same layout, a 2-byte big-endian length followed by
the message. So a packet is a 20-byte header plus a slice of the mapped file, sent with a scatter
write and no copying. Packets hold up to 1,400 bytes of blocks.

Sequence numbers start at 1. The publisher records each message's file offset in a `SequenceWindow`,
a ring of atomics, so the retransmission server can read a run of messages without any lock.
Heartbeats go out while pacing holds the publisher back. At the end, end-of-session packets persist
for a while so receivers can still fill their last gaps ("End of Session").

`--rate max` sends as fast as allowed, and `--max-msgs-per-s` caps that. `--rate 10x` paces by ITCH
timestamps.

Multicast has no flow control, so a sender can outrun its receiver, and the receiver's rate is the
book build's. If the sender stays faster for long enough, the receiver falls further behind than
the retransmission window reaches, and `itch listen` exits with code 5. That happened at 2M msgs/s
on a busy machine, when the book build ran at about 1.7M msgs/s: the receiver stopped at sequence
122,029,089 and the server refused 21 requests. The demo therefore paces at 1M msgs/s, which keeps
the run about recovery rather than about keeping up.

## Receiver

`MoldReceiver` has no sockets, which lets the unit tests and `fuzz_mold` reach every path. It follows
the "Receiver Example" in the specification:

1. The first packet sets the session. Packets from any other session are counted and ignored.
2. If a packet starts at the expected sequence, its messages are delivered. A packet that overlaps
   what was delivered is trimmed, and a packet entirely in the past counts as a duplicate.
3. If a packet starts later, it goes into a bounded buffer and the gap is requested, up to the first
   buffered packet. Heartbeats and end-of-session packets carry the next sequence, so they reveal
   loss at the tail too.
4. A response holds one packet's worth of messages ("Request Packet"), so each arrival triggers a
   request for whatever is still missing. A silent request is repeated on a timer.
5. When the retries run out, the gap must be older than the retransmission window, and `itch listen`
   exits with code 5. `MoldReceiver` never skips a gap, because a book built past one would be
   wrong with no sign of it.

One thread drains the socket into a ring, so the kernel buffer stays empty while the book is busy.

## What if retransmission is unavailable?

`itch listen` stops rather than carry on with a broken book. A real deployment would recover state
from Nasdaq's GLIMPSE snapshot service or its SoupBinTCP session and then rejoin the feed. Neither is
in scope here (FR-32 is a Could item).

## Verifying it

`scripts/mcast_smoke.sh` is the P-13 check, and `scripts/demo_gap_recovery.sh` is AC-H-03.
`tests/integration/loopback_test.cpp` runs both ends in one process under TSan. On this machine
multicast on loopback works under WSL2, and the kernel gave an 8 MiB receive buffer.

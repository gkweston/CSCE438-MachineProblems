# CSCE438 MP1 — Bokehchat!
A multhithreaded client server in C++.

*Note: We're not enforcing the C++11 standard in here so follow the makefile to use.*

## Compilation & Use
Compile: `make all`

Clean: `make clean`

For client and server, respectively:

        ./server <port>
        ./client <host> <port>

## Future Features
I might seek to add these features, personal time allowing:

**Simple UI**: Buffer the last 10 received messages and refresh the terminal screen when another message comes in. Create the appearance of an interactive chat client, IRC-style.

**Usernames**: Map UID to usernames server-side, given by client on `JOIN`.

**"... is typing"**: Ping the server with client UID when user interacts with `stdin`. Client-side sniffer threads receive a typing message with the respective username which is shared with the screen-refresh thread.

**Exit chatmode**: Either spawn a child that returns on `^C` or bind a quit command so users don't have to interrupt client.

**Password protected chatrooms**: *See below.*

**E2E Encryption**: A couple ideas on how this might be executed.

1. Enforce 16 byte server passwords for all encrypted chatrooms. Server can allow clients to join by comparing `HASH1(pass)` which is presented by client and saved on the server. The server can then distribute a 16 byte "salt-like" element (just call this `rand`). Clients could then send messages of form `ENC(Key=HASH2(pass||rand), Msg[])`. We will still only have 2^128 effective shared secrets +/- a few thousand/million depending on `rand`'s generation and usage. On some level, this approach just offloads secure secret generation to the user.

2. Clients are provisioned with a client-server key. Sever generates a new chatroom secret when users create a new encrypted room. Compare a hash of the chatroom password (as above) when clients join. Distribute the chatroom secret to clients encrypted with client-server key. Relies on the server being securely coded and unaccessible, along with client keys being safe in memory and safely provisioned. Neither of these should necessarily be assumed, but should be possible.

**Efficient UID generation**: Currently, there is a UID exchange between client and server which allows us to limit the UID size to 1 byte. A more efficient UID for a scalable system would be to instantiate 32 or 64 byte UIDs randomly by the client and circumvent this exchange. However, we would then be sending 32-64 more bytes on the wire for each message, which is unnecessary for this version.

## Other
Check out the design doc for a very high-level overview of how it currently works. For more granular details, reference documentation in code. Cheers!



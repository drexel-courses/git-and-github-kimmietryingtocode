# Network Protocol Analysis: TCP Remote Shell

## 1. Learning Process

I used ChatGPT-style AI guidance while doing this analysis. I asked questions, then validated answers directly against my own code and traces.

AI prompts I used:
1. "How do I trace a TCP client/server in C with strace and see command bytes?"
2. "Why does strace sometimes show sendto/recvfrom instead of send/recv?"
3. "How can I verify an EOF marker (0x04) in a custom TCP protocol?"
4. "How do message boundaries work in TCP streams, and why do I need delimiters?"

Resources suggested by AI:
- `man 2 socket`, `man 2 connect`, `man 2 accept`, `man 2 send`, `man 2 recv`
- `man 7 tcp`
- `man strace`

Challenges encountered:
- My first strace filter (`send,recv`) missed payload syscalls because libc used `sendto/recvfrom`.
- Server command output is written by child processes to socket fd via `write()` after `dup2()`, so I had to trace `write` too.
- `tcpdump` on loopback was permission-blocked in this environment, so packet-level capture was not possible here.

## 2. Protocol Design Analysis

### A. Protocol Specification

My implementation (from `rsh_cli.c`, `rsh_server.c`, `rshlib.h`) uses this protocol:

Client -> Server:
- Format: null-terminated command string
- Encoding: ASCII bytes
- Example: `"echo hello\0"`
- Code evidence: client sends `strlen(cmd)+1`

Server -> Client:
- Format: command output bytes, then one-byte EOF marker
- EOF marker: `0x04` (`RDSH_EOF_CHAR`)
- Example: `"hello\n"` followed by `"\x04"`

Why EOF is needed:
- TCP is a byte stream, not message-framed.
- Client may receive one response in many chunks, or multiple server sends in one recv.
- The client stops reading only when last received byte is `0x04`.

### B. Message Boundary Problem

TCP has no built-in message boundaries. This means:
- multiple server writes can merge into one client recv
- one server write can split across multiple client recv calls

My protocol solves this by appending `0x04` after each response. The client receive loop keeps reading until it sees that marker.

### C. Protocol Limitations

1. If command output contains byte `0x04`, client may terminate message early.
2. If connection drops mid-response, client may never receive EOF marker.
3. No length prefix, no checksum, no versioning.

Production improvements:
- Use length-prefixed frames (e.g., 4-byte length header).
- Escape/control-frame mechanism instead of raw sentinel collision risk.
- Add protocol version + explicit status/error frame.

## 3. Traffic Capture and Analysis

I used **Option B (strace)** and captured client/server syscall traces during:
- command 1: `echo hello`
- command 2: `stop-server`

### A. Connection Setup Evidence

Client trace:
```text
socket(AF_INET, SOCK_STREAM, IPPROTO_IP) = 3
connect(3, {sa_family=AF_INET, sin_port=htons(1234), sin_addr=inet_addr("127.0.0.1")}, 16) = 0
```

Server trace:
```text
socket(AF_INET, SOCK_STREAM, IPPROTO_IP) = 3
bind(3, {sa_family=AF_INET, sin_port=htons(1234), sin_addr=inet_addr("0.0.0.0")}, 16) = 0
listen(3, 20) = 0
accept(3, NULL, NULL) = 4
```

### B. Command `echo hello` End-to-End

1. Client sends command (`echo hello\0`):
```text
sendto(3, "\x65\x63\x68\x6f\x20\x68\x65\x6c\x6c\x6f\x00", 11, 0, NULL, 0) = 11
```
Hex bytes decode to `e c h o <space> h e l l o \0`.

2. Server receives command:
```text
recvfrom(4, "\x65\x63\x68\x6f\x20\x68\x65\x6c\x6c\x6f\x00", 65536, 0, NULL, NULL) = 11
```

3. Server sends response and EOF:
- Command output from child process to socket:
```text
write(1, "\x68\x65\x6c\x6c\x6f\x0a", 6) = 6
```
- EOF marker from parent:
```text
sendto(4, "\x04", 1, 0, NULL, 0) = 1
```

4. Client receives response then EOF in separate recv calls:
```text
recvfrom(3, "\x68\x65\x6c\x6c\x6f\x0a", 65536, 0, NULL, NULL) = 6
recvfrom(3, "\x04", 65536, 0, NULL, NULL) = 1
```
This proves the EOF byte terminates the receive loop.

### C. `stop-server` Command Evidence

Client sends:
```text
sendto(3, "\x73\x74\x6f\x70\x2d\x73\x65\x72\x76\x65\x72\x00", 12, 0, NULL, 0) = 12
```
Server receives and responds with EOF-only frame:
```text
recvfrom(4, "\x73\x74\x6f\x70\x2d\x73\x65\x72\x76\x65\x72\x00", 65536, 0, NULL, NULL) = 12
sendto(4, "\x04", 1, 0, NULL, 0) = 1
```
Client receives EOF and exits cleanly.

## 4. TCP Connection Verification

Checklist:
- [x] TCP connection established successfully (`connect` on client, `accept` on server)
- [x] Client sends null-terminated commands
- [x] Server responses include EOF marker (`0x04`)
- [x] Client receive loop terminates on EOF marker
- [x] Connection closes gracefully (`close()` on both sides after stop-server workflow)

Answers:
1. **How many TCP packets for connection establishment?**
   - Standard TCP handshake is 3 packets (SYN, SYN-ACK, ACK).
   - I could not capture packet headers directly because `tcpdump` permission was blocked in this environment, but successful `connect()`/`accept()` confirms handshake completion.

2. **How does TCP handle send() calls?**
   - Application writes are stream bytes. In my trace, server response arrived in two receives (`"hello\\n"` then `0x04`), showing framing is not guaranteed by send boundaries.

3. **Can you see EOF character?**
   - Yes. Server `sendto(... "\\x04", 1 ...)` and client `recvfrom(... "\\x04", ...)` directly show EOF byte.

4. **What happens on `exit`/teardown?**
   - In my run I used `stop-server` to stop both sides. Server sends final EOF, closes client socket, then closes listening socket and exits.

## Summary

The protocol works as designed:
- client commands are null-terminated
- server responses are EOF-delimited with `0x04`
- client correctly loops until EOF marker

Main risk is sentinel collision (`0x04` in payload). A length-prefixed protocol would be more robust for production.

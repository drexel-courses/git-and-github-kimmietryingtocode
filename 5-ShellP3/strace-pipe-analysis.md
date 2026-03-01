# strace Pipe Analysis

## 1. Learning Process

I used an AI assistant (Codex/GPT-5 in my IDE) to guide the `strace` workflow and interpret traces.

Prompts I used:
1. `How do I trace pipe setup and file descriptor redirection in a shell with strace?`
2. `Why do I need -f for pipeline debugging, and what syscalls should I filter on?`
3. `What does dup2(pipefd[1], STDOUT_FILENO) change in the process fd table?`
4. `Why does a pipeline hang if a write end of the pipe is left open in parent or siblings?`

Key references suggested by AI and used:
- `man strace`
- `man 2 pipe`
- `man 2 dup2`
- `man 2 close`
- `man 2 waitpid`

Challenges encountered:
- In this environment, `strace` required ptrace permission escalation.
- My first filter used `pipe`, but trace showed `pipe2` calls (glibc path), so I updated filters to include `pipe2`.
- Trace output is noisy (many `execve` PATH probes and loader `close()` calls), so I focused on the pipeline-setup window around `pipe2/dup2/close/clone/wait4`.

## 2. Basic Pipe Analysis

### A. Two-command pipeline: `ls | cat`

Command used:

```bash
printf 'ls | cat\nexit\n' | strace -f -e trace=pipe,pipe2,dup2,close,fork,vfork,clone,execve,wait4 -o /tmp/trace_ls_cat_pipe2.txt ./dsh
```

Relevant trace excerpt:

```text
2716508 pipe2([3, 4], 0)                = 0
2716508 clone(...)                      = 2716509
2716508 clone(...)                      = 2716510
2716509 dup2(4, 1)                      = 1
2716510 dup2(3, 0)                      = 0
2716508 close(3)                        = 0
2716508 close(4)                        = 0
2716510 close(3)                        = 0
2716510 close(4)                        = 0
2716509 close(3)                        = 0
2716509 close(4)                        = 0
2716509 execve(..., ["ls"], ...)         = 0   (eventually /usr/bin/ls)
2716510 execve(..., ["cat"], ...)        = 0   (eventually /usr/bin/cat)
2716508 wait4(2716509, ...)             = 2716509
2716508 wait4(2716510, ...)             = 2716510
```

Analysis:
- `pipe2([3,4],0)` created one pipe: read end `3`, write end `4`.
- Child `2716509` (`ls`) runs `dup2(4,1)` so stdout goes into the pipe.
- Child `2716510` (`cat`) runs `dup2(3,0)` so stdin comes from the pipe.
- Parent closes both pipe fds after forking.
- Each child also closes both original pipe fds after `dup2`.
- Both children are created and waited on.

### B. Three-command pipeline: `ls | grep txt | wc -l`

Command used:

```bash
printf 'ls | grep txt | wc -l\nexit\n' | strace -f -e trace=pipe,pipe2,dup2,close,fork,vfork,clone,execve,wait4 -o /tmp/trace_ls_grep_wc_pipe2.txt ./dsh
```

Relevant trace excerpt:

```text
2716452 pipe2([3, 4], 0)                = 0
2716452 pipe2([5, 6], 0)                = 0
2716452 clone(...)                      = 2716453
2716452 clone(...)                      = 2716454
2716452 clone(...)                      = 2716455
2716453 dup2(4, 1)                      = 1
2716454 dup2(3, 0)                      = 0
2716454 dup2(6, 1)                      = 1
2716455 dup2(5, 0)                      = 0
2716452 close(3)                        = 0
2716452 close(4)                        = 0
2716452 close(5)                        = 0
2716452 close(6)                        = 0
```

Analysis:
- There are **2 pipe calls** for **3 commands** (`N-1` rule).
- Pipe fd pairs: `[3,4]` and `[5,6]`.
- Middle command (`grep`, pid `2716454`) has both directions:
  - `dup2(3,0)` reads from first pipe.
  - `dup2(6,1)` writes to second pipe.
- Three children are created (PIDs `2716453`, `2716454`, `2716455`).

### C. File descriptor leak demo

I temporarily disabled the pipeline pipe-close loops in `execute_pipeline()`, rebuilt, and traced:

```bash
timeout 6s bash -lc "printf 'ls | cat\nexit\n' | strace -f -e trace=pipe,pipe2,dup2,close,fork,vfork,clone,execve,wait4,read -o /tmp/trace_leak_ls_cat.txt ./dsh"
```

Result:
- Command timed out with exit code `124` (hang reproduced).

Relevant trace evidence:

```text
2716727 pipe2([3, 4], 0)                = 0
2716728 dup2(4, 1)                      = 1
2716729 dup2(3, 0)                      = 0
...
2716728 +++ exited with 0 +++           # producer (ls) finished
2716727 wait4(2716729, ...)             = ? ERESTARTSYS
2716729 read(0, ...)                    = ? ERESTARTSYS
# killed by timeout SIGTERM
```

Why it hung:
- A write end of the pipe remained open (parent/sibling copies not closed).
- `cat` kept waiting for EOF on stdin.
- Parent blocked in `wait4` waiting for `cat` to terminate.

After the experiment, I restored original `dshlib.c` and rebuilt normal `dsh`.

## 3. File Descriptor Management

1. **When are pipes created?**
- Pipes are created before forking children.
- For `N` commands, shell creates `N-1` pipes.

2. **What fds do pipes use?**
- Observed pipe fds: `[3,4]`, `[5,6]`.
- They begin at 3 because `0,1,2` are stdin/stdout/stderr.

3. **How does `dup2()` work?**
- `dup2(4,1)` makes fd `1` (stdout) refer to same open file description as fd `4`.
- After duplication, original fd `4` is redundant and should be closed to avoid leaks.

4. **Which pipes are closed by each process?**
- First command: keeps stdout mapped to write-end of first pipe; closes all original pipe fds.
- Middle command: maps stdin from previous pipe read-end and stdout to next pipe write-end; closes all originals.
- Last command: keeps stdin mapped to read-end of last pipe; closes all originals.
- Parent: closes all pipe fds after forking all children, then waits.

5. **What if you forget to close a pipe?**
- Reader side process (often last command like `cat`) can hang waiting for EOF.
- Parent may hang in `waitpid/wait4` waiting for that reader to exit.
- In strace this appears as reader blocked in `read(0, ...)` and parent blocked in `wait4(...)`.

## 4. Pipeline Verification

Checklist:
- [x] `pipe()`/`pipe2()` called `N-1` times for `N` commands
- [x] Each child calls `dup2()` appropriately
- [x] All children close all pipe fds after `dup2`
- [x] Parent closes all pipe fds after forking
- [x] All children reach `execve()`
- [x] Parent waits for all children

Verification answers:
1. Correct number of pipes: **Yes** (`1` for 2-command, `2` for 3-command).
2. Correct redirection: **Yes** (`ls -> pipe write`, `cat <- pipe read`; middle `grep` does both).
3. Unused pipe ends closed: **Yes** in normal build.
4. FD leaks: **No leak in normal build**. Leak/hang reproduced only when close logic was intentionally disabled.

## Notes

- Commands and trace files used:
  - `/tmp/trace_ls_cat_pipe2.txt`
  - `/tmp/trace_ls_grep_wc_pipe2.txt`
  - `/tmp/trace_leak_ls_cat.txt`

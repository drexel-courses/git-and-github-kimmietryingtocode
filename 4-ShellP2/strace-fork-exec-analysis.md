# System Call Analysis with strace: Fork/Exec

## 1. Learning Process

I used an AI assistant (Codex/GPT-style chat in my IDE) to learn `strace` for process tracing.

Prompts I asked:
1. `What does strace show, and how is it different from gdb?`
2. `How do I trace child processes created by fork?`
3. `How do I filter strace to only process-related syscalls like fork/exec/wait?`
4. `In strace output, how do I tell parent and child apart by PID?`

Resources the AI pointed me to:
- `man strace`
- `man 2 fork`
- `man 2 execve`
- `man 3 execvp`
- `man 2 waitpid`

Challenges I hit while learning:
- At first I ran `strace` in the sandbox and got `Operation not permitted` because ptrace was restricted.
- I also learned that on this Linux setup, `fork()` appears as `clone(...)` in `strace`, so filtering only `fork` can miss process creation lines.
- It took me a minute to realize `execvp()` is a library call, but `strace` shows the kernel syscall `execve()`.

## 2. Basic Fork/Exec Analysis

Commands I ran to collect traces:

```bash
printf 'ls\nexit\n' | strace -f -e trace=clone,fork,vfork,execve,wait4 -o trace_process_simple.txt ./dsh
printf 'notacommand\nexit\n' | strace -f -e trace=clone,fork,vfork,execve,wait4 -o trace_process_notfound.txt ./dsh
printf 'echo "hello world"\nexit\n' | strace -f -e trace=fork,execve,wait4 -o trace_args.txt ./dsh
```

### A) Executing a simple command (`ls`)

Relevant output (`trace_process_simple.txt`):

```text
3461734 clone(...) = 3461737
3461734 wait4(3461737,  <unfinished ...>
3461737 execve("/usr/bin/ls", ["ls"], ...) = 0
3461737 +++ exited with 0 +++
3461734 <... wait4 resumed>[{WIFEXITED(s) && WEXITSTATUS(s) == 0}], 0, NULL) = 3461737
```

Analysis:
- Parent shell PID is `3461734`.
- Parent creates child with `clone(...) = 3461737` (this is the fork-equivalent on this system).
- Child PID `3461737` runs `execve("/usr/bin/ls", ["ls"], ...)`.
- Parent waits with `wait4(3461737, ...)` and gets exit status `0`.
- This confirms parent waits for the command to finish.

### B) Command not found (`notacommand`)

Relevant output (`trace_process_notfound.txt`):

```text
3461742 clone(...) = 3461743
3461743 execve("/usr/bin/notacommand", ["notacommand"], ...) = -1 ENOENT
...
3461743 execve("/snap/bin/notacommand", ["notacommand"], ...) = -1 ENOENT
3461743 +++ exited with 2 +++
3461742 <... wait4 resumed>[{WIFEXITED(s) && WEXITSTATUS(s) == 2}], 0, NULL) = 3461743
```

Analysis:
- `execve()` keeps failing with `ENOENT` (file not found) for each PATH directory.
- Child exits with code `2` in my implementation (`_exit(errno)` after failed `execvp`, and `errno` ended as `ENOENT=2`).
- Parent collects that status with `wait4()` and then prints the command-not-found message.

### C) Command with arguments (`echo "hello world"`)

Relevant output (`trace_args.txt`):

```text
3460191 wait4(3460192,  <unfinished ...>
3460192 execve("/usr/bin/echo", ["echo", "hello world"], ...) = 0
3460192 +++ exited with 0 +++
3460191 <... wait4 resumed>[{WIFEXITED(s) && WEXITSTATUS(s) == 0}], 0, NULL) = 3460192
```

Analysis:
- Arguments are passed as an argv array: `["echo", "hello world"]`.
- This confirms quote parsing worked: `hello world` stayed one argument.

## 3. PATH Search Investigation

I inspected PATH-search behavior using the trace logs above (`trace_process_simple.txt`, `trace_process_notfound.txt`, `trace_args.txt`) and my environment `PATH`.

### A) What `execvp()` does during search

Observed behavior:
- `execvp()` attempted multiple `execve("<dir>/command", ...)` calls.
- Each failed location returned `-1 ENOENT` until a valid executable was found.
- For `ls`, success was at `/usr/bin/ls`.

Findings:
1. **What syscalls before success?**
- In my trace, I mainly saw repeated `execve(...)` attempts (not separate `access()` calls).
- So glibc resolved PATH by trying candidate paths directly via `execve`.

2. **How many directories checked?**
- `ls`: 9 `execve` path attempts total (final one succeeded).
- `echo`: 9 attempts total (final one succeeded at `/usr/bin/echo`).
- `notacommand`: 15 attempts total, all failed.

3. **Error when not found?**
- `execve(...) = -1 ENOENT (No such file or directory)`.

4. **Which directory succeeded?**
- `ls` succeeded at `/usr/bin/ls`.
- `echo` succeeded at `/usr/bin/echo`.

### B) PATH explanation

- `PATH` is an environment variable listing directories to search for executables.
- My shell calls `execvp(cmd, argv)`, so if command has no slash (like `ls`), it searches each PATH directory in order.
- That is why I can type `ls` instead of `/usr/bin/ls`.
- If command is not in any PATH directory, all `execve` attempts fail with `ENOENT`, and the command fails.

## 4. Parent/Child Process Verification

Checklist:
- [x] fork-equivalent is called once per command (`clone(...)` on this system)
- [x] parent receives child PID from process-creation call
- [x] child calls `execve()` (from my `execvp()`)
- [x] parent calls `wait4()` (from my `waitpid()`)
- [x] parent waits after child creation
- [x] child PID in `wait4()` matches PID returned by `clone(...)`

Answers:
1. **Does implementation create child correctly?**
- Yes. Parent creates exactly one child for each external command.

2. **Does child replace itself with command?**
- Yes. Child calls `execve()` for the target command (`/usr/bin/ls`, `/usr/bin/echo`, etc.).

3. **Does parent wait for child to complete?**
- Yes. `wait4(child_pid, ...)` appears immediately after child creation and reports child exit status.

4. **Any unexpected system calls?**
- Expected Linux detail: `clone()` appears instead of `fork()` in trace.
- Also observed `SIGCHLD` delivery after child exits, which is normal.

## Final Reflection

From this trace work, I verified that my shell’s `fork/exec/wait` model is correct at OS level:
- create child
- child execs command
- parent waits and reads exit status

The strace output matched my C code behavior, including command-not-found error propagation through `errno`.

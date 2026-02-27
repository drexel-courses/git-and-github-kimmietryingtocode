1. Can you think of why we use `fork/execvp` instead of just calling `execvp` directly? What value do you think the `fork` provides?

    > **Answer**:  We use `fork()` first so the shell can create a child process to run the command, while the parent shell keeps running. If we called `execvp()` directly in the shell process, the shell itself would get replaced by that program and we would lose our prompt after one command. So `fork` gives us process separation: child runs command, parent stays as shell.

2. What happens if the fork() system call fails? How does your implementation handle this scenario?

    > **Answer**:  If `fork()` fails, it returns `-1` (usually because of resource limits like too many processes or low memory). In my code, I check that case in the `else` branch, call `perror("fork")`, set `rc = ERR_EXEC_CMD`, and set `last_status = errno`. So it reports the system error and keeps the shell loop alive.

3. How does execvp() find the command to execute? What system environment variable plays a role in this process?

    > **Answer**:  `execvp()` searches directories listed in the `PATH` environment variable when the command does not include a slash. It checks each directory in order until it finds an executable file with that name. So `PATH` is the key variable that controls where commands are found.

4. What is the purpose of calling wait() in the parent process after forking? What would happen if we didn’t call it?

    > **Answer**:  The parent calls `waitpid()` to wait for the child command to finish and collect its exit status. This prevents zombie processes and lets the shell know if the command succeeded or failed (`last_status`). If we did not wait, children could become zombies, and the shell would not correctly track return codes.

5. In the referenced demo code we used WEXITSTATUS(). What information does this provide, and why is it important?

    > **Answer**:  `WEXITSTATUS(status)` extracts the child’s numeric exit code (after checking `WIFEXITED(status)`). This is important because shells use that number to decide success (`0`) vs failure (non-zero), and features like your `rc` built-in depend on it.

6. Describe how your implementation of build_cmd_buff() handles quoted arguments. Why is this necessary?

    > **Answer**:  My parser tracks whether we are inside quotes (`'` or `"`). While inside quotes, spaces are treated as normal characters, not argument separators. It also strips the quote characters themselves, so `"hello world"` becomes one argument `hello world`. This is necessary so filenames/phrases with spaces are passed correctly as a single argv entry.

7. What changes did you make to your parsing logic compared to the previous assignment? Were there any unexpected challenges in refactoring your old code?

    > **Answer**:  I moved from simple whitespace tokenizing (`strtok`-style behavior) to a manual character-by-character parser. The new logic trims leading/trailing spaces, supports multiple spaces, handles both single and double quotes, writes tokens in-place into `_cmd_buffer`, and enforces `CMD_ARGV_MAX`/`SH_CMD_MAX` limits. The main challenge was making quote handling and in-place buffer writes work together without off-by-one bugs or accidentally dropping token boundaries.

8. For this quesiton, you need to do some research on Linux signals. You can use [this google search](https://www.google.com/search?q=Linux+signals+overview+site%3Aman7.org+OR+site%3Alinux.die.net+OR+site%3Atldp.org&oq=Linux+signals+overview+site%3Aman7.org+OR+site%3Alinux.die.net+OR+site%3Atldp.org&gs_lcrp=EgZjaHJvbWUyBggAEEUYOdIBBzc2MGowajeoAgCwAgA&sourceid=chrome&ie=UTF-8) to get started.

- What is the purpose of signals in a Linux system, and how do they differ from other forms of interprocess communication (IPC)?

    > **Answer**:  Signals are asynchronous notifications sent to a process to tell it that an event happened (example: user pressed Ctrl+C, timer expired, child exited). They are different from most IPC because they usually carry very little data (often just the signal number) and are mainly for interruption/control, not rich message passing like pipes, sockets, or shared memory.

- Find and describe three commonly used signals (e.g., SIGKILL, SIGTERM, SIGINT). What are their typical use cases?

    > **Answer**:  
    `SIGINT` (2): Sent by Ctrl+C in terminal. Typical use: user interrupts a foreground program.

    `SIGTERM` (15): Default polite terminate request (like `kill <pid>`). Typical use: ask a process to shut down cleanly so it can release resources.

    `SIGKILL` (9): Forced immediate kill (`kill -9 <pid>`). Typical use: last resort when process ignores/does not handle normal termination.

- What happens when a process receives SIGSTOP? Can it be caught or ignored like SIGINT? Why or why not?

    > **Answer**:  `SIGSTOP` pauses (stops) a process immediately. It cannot be caught, blocked, or ignored. Unlike `SIGINT`, the process does not get to run a handler for cleanup. This is by design so the OS/shell can always reliably suspend a process (for job control), and continue it later with `SIGCONT`.

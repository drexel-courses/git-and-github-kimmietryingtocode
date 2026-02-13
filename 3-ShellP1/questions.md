1. In this assignment I suggested you use `fgets()` to get user input in the main while loop. Why is `fgets()` a good choice for this application?

    > **Answer**: `fgets()` is a good fit because it reads a whole line safely and respects a max buffer size, so it helps prevent buffer overflow bugs. For a shell loop, we usually want one full command line at a time (including spaces), and `fgets()` does exactly that. It also keeps things simpler than character-by-character input, and we can just trim the trailing newline before parsing.

2. You needed to use `malloc()` to allocte memory for `cmd_buff` in `dsh_cli.c`. Can you explain why you needed to do that, instead of allocating a fixed-size array?

    > **Answer**: In this project, `cmd_buff` contains pointers (like `_cmd_buffer`) that need dynamic memory and clean lifetime control. Using `malloc()` lets us allocate exactly what the parser needs and free it when done, especially when building multiple commands in a pipeline. A fixed stack array might work for one simple case, but it becomes less flexible and easier to break when commands are reused, passed around, or need to outlive a single scope.


3. In `dshlib.c`, the function `build_cmd_list(`)` must trim leading and trailing spaces from each command before storing it. Why is this necessary? If we didn't trim spaces, what kind of issues might arise when executing commands in our shell?

    > **Answer**: Trimming is important so tokens are parsed correctly and command matching works reliably. Without trimming, something like `"   ls"` might be treated weirdly, empty pipe segments like `"ls |   | wc"` become harder to detect, and built-ins like `exit` might fail if extra spaces stay attached. It can also create empty/garbage argv entries that lead to bad `exec` calls or confusing errors.

4. For this question you need to do some research on STDIN, STDOUT, and STDERR in Linux. We've learned this week that shells are "robust brokers of input and output". Google _"linux shell stdin stdout stderr explained"_ to get started.

- One topic you should have found information on is "redirection". Please provide at least 3 redirection examples that we should implement in our custom shell, and explain what challenges we might have implementing them.

    > **Answer**: Three useful redirections:
    > 1. `cmd > out.txt` (STDOUT overwrite).  
    > Challenge: detect `>` in parsing, open file with the right flags (`O_CREAT | O_TRUNC | O_WRONLY`), and make sure only the child process gets redirected.
    >
    > 2. `cmd >> out.txt` (STDOUT append).  
    > Challenge: handle `>>` as a two-character token (not two `>` tokens), and open with `O_APPEND`.
    >
    > 3. `cmd < input.txt` (STDIN from file).  
    > Challenge: open file read-only, fail cleanly if file doesn’t exist, and wire it to fd 0 using `dup2`.
    >
    > Bonus important one: `cmd 2> err.txt` (STDERR redirect).  
    > Challenge: parse fd-specific redirection and keep stdout/stderr behavior separate.

- You should have also learned about "pipes". Redirection and piping both involve controlling input and output in the shell, but they serve different purposes. Explain the key differences between redirection and piping.

    > **Answer**: Redirection connects a command’s input/output to files (or sometimes specific fds). Piping connects one command’s output directly into another command’s input in memory through a pipe. So redirection is usually command-to-file, while piping is command-to-command for chaining work like `cat file | grep x | wc -l`.

- STDERR is often used for error messages, while STDOUT is for regular output. Why is it important to keep these separate in a shell?

    > **Answer**: Keeping them separate makes automation and debugging much easier. Scripts can capture normal output without mixing in errors, and users can redirect errors somewhere else (`2> errors.log`) while still seeing real results on screen. If everything is mixed together, parsing output and diagnosing failures gets messy fast.

- How should our custom shell handle errors from commands that fail? Consider cases where a command outputs both STDOUT and STDERR. Should we provide a way to merge them, and if so, how?

    > **Answer**: The shell should preserve both streams by default: normal output to STDOUT and errors to STDERR. It should also report command failure status (exit code) clearly. Yes, we should support merging when requested, like `2>&1`, because users sometimes want one combined log stream. Important detail: merge only when the user explicitly asks, not automatically.

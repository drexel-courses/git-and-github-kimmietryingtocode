# Shell Fundamentals Investigation

## 1. Learning Process (what I asked + how I felt)
- Tools: ChatGPT (this session) and a couple quick `man bash` skims; mostly relied on asking basic questions here.
- Prompts I actually used / paraphrased:
  1. “What is a Unix shell and why do we need it?”
  2. “Shell vs terminal emulator… are they the same thing?”
  3. “How do shells parse commands with quotes and spaces?”
  4. “Why is `cd` a built-in? What breaks if it’s external?”
  5. “What is BusyBox and why is it one binary?”
- AI nudged me to read bash man page sections on invocation and quoting, and to try `type cd` vs `type ls` in a real shell.
- Most surprising: realizing `cd` has to be built-in because a child process changing directories does nothing for the parent. Also, BusyBox really is one binary pretending to be many via `argv[0]`.

## 2. Shell Purpose and Design
### A. What is a shell / why it exists
- A shell = command-line interpreter that turns human text into syscalls (via programs) between user and kernel.
- OS needs it as the main interactive UI for running programs, wiring I/O, and scripting automation.
- Layering: user → terminal emulator (just handles screen/keyboard) → shell (parses/launches) → kernel (syscalls).

### B. Responsibilities
1. Parse input (tokenize, handle quotes/escapes, recognize metacharacters like `|`/`>`/`&`).
2. Launch processes (fork/exec external commands) and manage environment/exit codes.
3. I/O plumbing (redirection, pipes).
4. Job control (fg/bg, signals) in richer shells.

### C. Shell vs terminal
- Terminal/terminal emulator = the window/tty that draws text and sends keystrokes.
- Shell = the process running inside that terminal interpreting commands. You can swap shells inside the same terminal tab.

## 3. Command Line Parsing
### A. Tokenization
- Break input on unquoted whitespace into tokens (command + args). Collapses multiple spaces unless quoted.

### B. Quotes
- Single quotes: literal, no expansion; preserves spaces and symbols.
- Double quotes: preserves spaces but still expands variables/command substitution.
- Examples:
  - `echo $HOME` → expands path.
  - `echo '$HOME'` → prints `$HOME`.
  - `echo "$HOME"` → expands.
  - `echo "a    b"` keeps spaces between a and b.

### C. Metacharacters
- `|` pipe stdout to stdin of next command.
- `>` redirect stdout to file.
- `<` redirect stdin from file.
- `&` background job (run without waiting).
- `;` command separator (run sequentially).

### D. Edge cases
- Spaces in filenames: need quotes or escapes (`my file.txt` → `"my file.txt"`).
- Escaping: backslash removes special meaning of next char (e.g., `\|`).
- Empty input: shell warns or does nothing; stray pipe with nothing on one side is an error.

Command examples I tried while learning:
```bash
type cd
type ls
echo '$HOME'
echo "$HOME"
echo hello     world
echo "hello     world"
ls | grep txt
```

## 4. Built-in vs External Commands
### A. Definitions
- Built-in: implemented inside the shell process (no fork/exec).
- External: separate binaries searched via `$PATH`, run with fork/exec.

### B. Why built-ins exist
- Need to mutate shell state (cwd, vars) or reduce overhead (no new process).
- Some features impossible as externals (changing parent env).

### C. `cd` example
- If `cd` were external: child process would change dirs and exit; parent shell stays put → useless. So it must be built-in.

### D. Examples
- Built-ins: `cd`, `exit`, `echo` (often), `export`, `alias`.
- Externals: `ls`, `grep`, `cat`, `gcc`, `python`.
- Check with `type cmd` to see “shell builtin” vs a path.

## 5. Different Shells
### A. Common shells
- `sh` (Bourne/POSIX shell style), `bash`, `zsh`, and `fish` are the ones I saw most often in examples and class discussion.

### B. Bash vs zsh
- `bash` is the default on many Linux systems and is very common for scripts and class materials.
- `zsh` is usually more interactive-friendly out of the box (better completion and prompt customization).
- From a student perspective, bash felt like the “baseline everyone supports,” while zsh felt nicer for day-to-day terminal use.

### C. What makes fish different
- `fish` focuses on usability first: autosuggestions, syntax highlighting, and simpler scripting ideas.
- It is great for interactive use, but it is not a drop-in replacement for POSIX shell scripts in all cases.

### D. Why `sh` is still important
- `sh` matters because POSIX shell compatibility is still a big deal for portability.
- A lot of install scripts and system scripts target `/bin/sh`, so understanding `sh` behavior helps avoid writing shell code that only works on one shell.

## 6. BusyBox Investigation
### A. What it is
- Single binary providing many Unix applets (marketed as the “Swiss Army knife of embedded Linux”).
- Different from traditional: instead of many separate binaries, one executable houses many small implementations.

### B. Why it exists
- Solve space/footprint constraints in embedded/rescue systems; full GNU coreutils are much larger.

### C. Where it’s used
- Alpine/Docker minimal images.
- Home routers/IoT (e.g., OpenWrt).
- Rescue/initramfs/recovery environments.

### D. How it works
- One binary; `argv[0]` (or symlink name) selects which applet to run (e.g., symlink `ls` → BusyBox dispatches to its `ls` code).
- Can also call `busybox ls` explicitly.

### E. Trade-offs
- Pros: tiny size, single deployable, consistent minimal feature set, great for constrained systems.
- Cons: fewer features than full GNU/BSD tools; occasional compatibility gaps with scripts expecting GNU options; not always POSIX-perfect.

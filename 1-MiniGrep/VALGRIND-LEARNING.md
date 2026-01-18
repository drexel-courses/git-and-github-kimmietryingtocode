# Valgrind Learning Journey (MiniGrep)

## My Discovery Process
- I started by clarifying how to use Valgrind effectively for a small C utility (`minigrep`). I asked AI tools focused questions to narrow the scope to memory debugging and leak detection.
- I iterated on flags and outputs to ensure I understood what Valgrind was reporting and why.

### Questions I asked AI tools
- "What Valgrind flags should I use to detect leaks and invalid memory accesses in a simple C program?"
- "How do I interpret Valgrind’s leak summary: definitely lost vs indirectly lost vs possibly lost vs still reachable?"
- "What does ‘Conditional jump or move depends on uninitialized value(s)’ mean, and how do I fix it?"

### Example prompts I used
1. "Explain Valgrind output for a program that `malloc`s a buffer and frees it at the end. Why might `still reachable` appear even if I call `free`?"
2. "Given `fgets(buffer)` in a loop, how can I avoid off-by-one or uninitialized read issues Valgrind might flag?"
3. "Show a minimal checklist to confirm a clean Valgrind run (no leaks, no invalid reads/writes)."

## Memory Errors I Found
- I ran Valgrind against the compiled `minigrep` binary, focusing on typical failure modes: forgetting to `free`, failing to `fclose`, or reading/writing past buffer bounds.
- In this implementation, Valgrind did not report memory leaks or invalid accesses on the tested paths. The program:
  - `malloc`s a single `line_buffer` and frees it once after processing all files.
  - Uses `fopen`/`fclose` consistently, including error branches.
  - Iterates via pointer arithmetic in `str_match` and avoids out-of-bounds writes.

### If errors had appeared (reference)
- "Invalid read of size N": Typically indicates reading past the end of a buffer. Fix by ensuring loop bounds and termination checks (`*p != '\0'`) are correct.
- "Conditional jump depends on uninitialized value(s)": Usually reading uninitialized memory (e.g., not setting `line_buffer` before use). Fix by relying on `fgets` return value and not accessing the buffer unless it returned non-NULL.
- "definitely lost": Memory allocated and not freed anywhere. Fix by calling `free` on all `malloc` paths, including early returns and error exits.

Since the program uses a single allocation, frees it, and closes files, the run was clean. If your environment ever shows `still reachable` bytes, it often comes from libc-internal allocations that persist until process exit; enabling `--show-reachable=yes` can make those visible, and they are not leaks in user code.

## Technical Understanding
- Valgrind detects a range of memory issues:
  - Invalid reads/writes (out-of-bounds, use-after-free)
  - Use of uninitialized values
  - Memory leaks (definitely/indirectly/possibly lost)
  - Overlapping `memcpy`/`memmove` misuse (via `memcheck`)
- Valgrind helps you write better C code by making hidden bugs observable. It enforces disciplined resource management: every `malloc` should have a matching `free`, every `fopen` a matching `fclose`, and buffer handling must be within bounds.
- Difference between leak categories:
  - "definitely lost": Memory that has no pointers referencing it; you cannot free it anymore. This is a true leak in your code.
  - "still reachable": Memory that remains referenced at program exit (often from libraries). It is not necessarily a bug; user code may be clean even if this is non-zero.

## Evidence: Clean Valgrind Runs
- Commands exercised: `./minigrep "test" testfile.txt`, `./minigrep -n "test" testfile.txt`, `./minigrep -i "TEST" testfile.txt`, `./minigrep -c "test" testfile.txt`
- Flags used for all runs: `--leak-check=full --show-leak-kinds=all --track-origins=yes`
- Example clean output (all runs show the same leak/error summary):

```text
$ valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./minigrep -n "test" testfile.txt
==30161== HEAP SUMMARY:
==30161==     in use at exit: 0 bytes in 0 blocks
==30161==   total heap usage: 3 allocs, 3 frees, 8,920 bytes allocated
==30161== 
==30161== All heap blocks were freed -- no leaks are possible
==30161== 
==30161== For lists of detected and suppressed errors, rerun with: -s
==30161== ERROR SUMMARY: 0 errors from 0 contexts (suppressed: 0 from 0)
```


# Questions

Answer the following questions about your minigrep implementation:

## 1. Pointer Arithmetic in Pattern Matching

In your `str_match()` function, you need to check if the pattern appears anywhere in the line, not just at the beginning. Explain your approach for checking all possible positions in the line. How do you use pointer arithmetic to advance through the line?

> In str_match(), I treat every character in the line as a possible starting point for the pattern. I use one pointer that walks through the line one character at a time, and at each position I start a second comparison using two more pointers: one that moves forward through the line and one that moves forward through the pattern. If all characters match until I hit the end of the pattern, I return 1. If I hit a mismatch, I stop that attempt and move the starting pointer forward by one and try again. The main idea is that instead of using indexes like line[i], I keep doing pointer moves like start++, current_line_pos++, and current_pattern_pos++.

## 2. Case-Insensitive Comparison

When implementing case-insensitive matching (the `-i` flag), you need to compare characters without worrying about case. Explain how you handle the case where the pattern is "error" but the line contains "ERROR" or "Error". What functions did you use and why?

> For case-insensitive matching, I compare characters after converting them to the same case. So if the pattern is "error" and the line has "ERROR" or "Error", I convert both characters to lowercase before checking if they are equal. I used tolower() because it is a simple way to normalize letters into one consistent form. I also cast to unsigned char before calling tolower() because that is the safe way to avoid weird behavior with non-ASCII characters.

## 3. Memory Management

Your program allocates a line buffer using `malloc()`. Explain what would happen if you forgot to call `free()` before your program exits. Would this cause a problem for:
   - A program that runs once and exits?
   - A program that runs in a loop processing thousands of files?

> If I forget to call free(), the memory I allocated with malloc() stays allocated until the program ends.

For a program that runs once and exits, it usually will not look like a big problem because the operating system reclaims the memory after the process finishes. Although, it is still bad practice.

For a program that runs in a loop processing thousands of files, it becomes a real problem. If you keep allocating buffers and never freeing them, memory usage keeps growing and the program can slow down, start failing allocations, or even get killed for using too much memory.

## 4. Buffer Size Choice

The starter code defines `LINE_BUFFER_SZ` as 256 bytes. What happens if a line in the input file is longer than 256 characters? How does `fgets()` handle this situation? (You may need to look up the documentation for `fgets()` to answer this.)

> If a line is longer than 256 characters, fgets() does not read the whole line in one call. It reads up to LINE_BUFFER_SZ - 1 characters, then adds a null terminator. If it hits a newline before that limit, the newline is included in the buffer. If the line is too long, the newline will not be included because it has not been reached yet, and the next fgets() call will read the rest of that same line. So long lines get split across multiple reads, and my program will treat each chunk like its own “line” for matching.

## 5. Return Codes

The program uses different exit codes (0, 1, 2, 3, 4) for different situations. Why is it useful for command-line utilities to return different codes instead of always returning 0 or 1? Give a practical example of how you might use these return codes in a shell script.

> Different exit codes are useful because they let other programs and scripts understand what happened without needing to parse printed output. It is a clean way to communicate success versus different kinds of failure. For example, an exit code of 3 can mean the file could not be opened, while 1 can mean the pattern simply was not found.

./minigrep "ERROR" logfile.txt
exit_code=$?

if [ $exit_code -eq 0 ]; then
  echo "Found ERROR lines"
elif [ $exit_code -eq 1 ]; then
  echo "No ERROR lines found"
elif [ $exit_code -eq 3 ]; then
  echo "File problem, could not open logfile.txt"
else
  echo "Something else went wrong, code was $exit_code"
fi
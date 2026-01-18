#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#define LINE_BUFFER_SZ 256

// Function prototypes
void usage(char *exename);
int str_len(char *str);
int str_match(char *line, char *pattern, int case_insensitive);

/**
 * usage - prints usage information
 * @exename: the name of the executable
 */
void usage(char *exename) {
    printf("usage: %s [-h|n|i|c|v] \"pattern\" filename\n", exename);
    printf("  -h    prints this help message\n");
    printf("  -n    prints matching lines with line numbers\n");
    printf("  -i    case-insensitive search\n");
    printf("  -c    counts matching lines\n");
    printf("  -v    inverts match (prints non-matching lines) [EXTRA CREDIT]\n");
}

/**
 * str_len - calculates the length of a string
 * @str: pointer to null-terminated string
 * 
 * Returns: length of string (not including null terminator)
 * 
 * TODO: IMPLEMENT THIS FUNCTION
 * You must use pointer arithmetic, no array notation
 * Do NOT use strlen() from standard library
 */
int str_len(char *str) {
    // TODO: Implement string length calculation
    if (str == NULL) return 0;
    char * p = str;
    while(*p) {
        p++;
    }
    return p - str;
}

/**
 * str_match - searches for pattern in line
 * @line: the line to search in
 * @pattern: the pattern to search for
 * @case_insensitive: if 1, ignore case; if 0, case-sensitive
 * 
 * Returns: 1 if pattern found, 0 if not found
 * 
 * TODO: IMPLEMENT THIS FUNCTION
 * You must use pointer arithmetic, no array notation
 * Hint: For case-insensitive, use tolower() or toupper() on both characters
 * Hint: You need to check if pattern matches starting at ANY position in line
 */
int str_match(char *line, char *pattern, int case_insensitive) {
    // TODO: Implement pattern matching
    // Remember: pattern can appear anywhere in the line
    // For case-insensitive, compare characters after converting to same case
    char* start;
    if (line == NULL || pattern == NULL) return 0;

    if (*pattern == '\0') {
        return 1;
    }

    for (start = line; *start != '\0'; start++) {
        char *current_line_pos = start;
        char *current_pattern_pos = pattern;

        /*
         * Compare forward while characters match.
         * Stop when:
         * - pattern ends (success), or
         * - line ends (fail for this start), or
         * - a mismatch occurs (fail for this start)
         */
        while (*current_pattern_pos != '\0' && *current_line_pos != '\0') {
            char line_character = *current_line_pos;
            char pattern_character = *current_pattern_pos;

            /* If case-insensitive, normalize both chars before comparing */
            if (case_insensitive) {
                line_character = (char)tolower((unsigned char)line_character);
                pattern_character = (char)tolower((unsigned char)pattern_character);
            }

            if (line_character != pattern_character) {
                break;
            }

            current_line_pos++;
            current_pattern_pos++;
        }

        // Return match value when reaching the end of pattern
        if (*current_pattern_pos == '\0') {
            return 1;
        }
    }

    //Tried every starting position, no full match
    return 0;
}

int main(int argc, char *argv[]) {
    char *line_buffer;      // buffer for reading lines from file
    char *pattern;          // the search pattern
    char *filename;         // the file to search
    FILE *fp;               // file pointer
    int show_line_nums = 0; // flag for -n option
    int case_insensitive = 0; // flag for -i option
    int count_only = 0;     // flag for -c option
    int invert_match = 0;   // flag for -v option (extra credit)
    int line_number = 0;    // current line number in file
    int match_count = 0;    // count of matching lines
    int found_match;        // result from str_match()
    int multiple_files = 0;     // flag for multiple files
    int total_matches = 0; // total matches across all files
    
    // Check minimum arguments
    if (argc < 2) {
        usage(argv[0]);
        exit(2);
    }
    
    // Handle -h flag
    if (*argv[1] == '-' && *(argv[1] + 1) == 'h') {
        usage(argv[0]);
        exit(0);
    }
    
    // Parse command line arguments
    int arg_idx = 1;  // current argument index
    
    // Check for option flags (they start with -)
    if (*argv[arg_idx] == '-') {
        char *flag_ptr = argv[arg_idx] + 1;  // skip the '-'
        
        // Process each character in the flag
        while (*flag_ptr != '\0') {
            switch (*flag_ptr) {
                case 'n':
                    show_line_nums = 1;
                    break;
                case 'i':
                    case_insensitive = 1;
                    break;
                case 'c':
                    count_only = 1;
                    break;
                case 'v':
                    invert_match = 1;  // extra credit
                    break;
                default:
                    printf("Error: Unknown option -%c\n", *flag_ptr);
                    usage(argv[0]);
                    exit(2);
            }
            flag_ptr++;  // move to next character in flag
        }
        arg_idx++;  // move to next argument
    }
    
    // Check we have pattern and filename
    if (argc < arg_idx + 2) {
        printf("Error: Missing pattern or filename\n");
        usage(argv[0]);
        exit(2);
    }
    
    pattern = argv[arg_idx];
    
    arg_idx++;

    if (argc - arg_idx > 1) {
        multiple_files = 1;
    }
    line_buffer = (char *)malloc(LINE_BUFFER_SZ);
    if (line_buffer == NULL) {
        printf("Error: Memory allocation failed\n");
        exit(4);
    }
    while (arg_idx < argc) {
    // TODO: Allocate memory for line buffer using malloc
    // Use LINE_BUFFER_SZ for the size
    // Check if malloc succeeds, if not exit with code 4
        filename = argv[arg_idx];
        line_number = 0;
        match_count = 0;
        
        // TODO: Open the file
        // Use fopen() with "r" mode
        // Check if fopen succeeds, if not print error and exit with code 3
        
        fp = fopen(filename, "r");
        if (fp == NULL) {
            printf("Error: Could not open file %s\n", filename);
            free(line_buffer);
            exit(3);
        }
        
        // TODO: Read file line by line
        // Use fgets() to read into line_buffer
        // Keep track of line_number (starts at 1)
        // For each line:
        //   - Call str_match() to check if pattern exists
        //   - Based on the flags (show_line_nums, case_insensitive, count_only, invert_match)
        //     print the appropriate output
        //   - Update match_count if needed
        //
        // Hint: fgets() returns NULL when end of file is reached
        // Hint: fgets() includes the newline character, you may want to handle that
        while(fgets(line_buffer, LINE_BUFFER_SZ, fp) != NULL) {
            line_number++;
            found_match = str_match(line_buffer, pattern, case_insensitive);
            if (invert_match) {
                found_match = !found_match;
            }
            if (found_match) {
                match_count++;
                total_matches++;
                if (!count_only) {
                    if (multiple_files) {
                        printf("%s:", filename);
                    }
                    if (show_line_nums) {
                        printf("%d:", line_number);
                    }
                    printf("%s", line_buffer);
                }
            }
        }
        
        // TODO: Close the file using fclose()
        fclose(fp);
        
        // TODO: If count_only flag is set, print the match count
        // Format: "Matches found: X" or "No matches found" if count is 0
        if (count_only) {
            if (multiple_files) {
                    printf("%s:", filename);
            }
            
            if (match_count > 0) {
                printf("Matches found: %d\n", match_count);
            } else {
                printf("No matches found\n");
            }
        }
        arg_idx++;
    }
    
    
    // TODO: Free the line buffer
    free(line_buffer);
    
    // Exit with appropriate code
    // 0 = success (found matches)
    // 1 = pattern not found
    if (total_matches > 0) {
        exit(0);
    } else {
        exit(1);
    }
    

}
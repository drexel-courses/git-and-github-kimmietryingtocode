#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <errno.h>
#include "dshlib.h"

// Local fallback; header doesn't define this string
#ifndef CMD_ERR_EXECUTE
#define CMD_ERR_EXECUTE "error: execution failure\n"
#endif

// Track last return code for extra credit "rc" builtin
static int last_status = 0;

//===================================================================
// HELPER FUNCTIONS - Memory Management (PROVIDED)
//===================================================================

/**
 * alloc_cmd_buff - Allocate memory for cmd_buff internal buffer
 * 
 * This function is provided for you. It allocates the _cmd_buffer
 * that will store the command string.
 */
int alloc_cmd_buff(cmd_buff_t *cmd_buff)
{
    cmd_buff->_cmd_buffer = malloc(SH_CMD_MAX);
    if (cmd_buff->_cmd_buffer == NULL) {
        return ERR_MEMORY;
    }
    cmd_buff->argc = 0;
    for (int i = 0; i < CMD_ARGV_MAX; i++) {
        cmd_buff->argv[i] = NULL;
    }
    return OK;
}

/**
 * free_cmd_buff - Free cmd_buff internal buffer
 * 
 * This function is provided for you. Call it when done with a cmd_buff.
 */
int free_cmd_buff(cmd_buff_t *cmd_buff)
{
    if (cmd_buff->_cmd_buffer != NULL) {
        free(cmd_buff->_cmd_buffer);
        cmd_buff->_cmd_buffer = NULL;
    }
    cmd_buff->argc = 0;
    return OK;
}

/**
 * clear_cmd_buff - Reset cmd_buff without freeing memory
 * 
 * This function is provided for you.
 */
int clear_cmd_buff(cmd_buff_t *cmd_buff)
{
    cmd_buff->argc = 0;
    for (int i = 0; i < CMD_ARGV_MAX; i++) {
        cmd_buff->argv[i] = NULL;
    }
    return OK;
}


//===================================================================
// PARSING FUNCTIONS - YOU IMPLEMENT THESE
//===================================================================

/**
 * build_cmd_buff - Parse a single command string into cmd_buff_t
 * 
 * YOU NEED TO IMPLEMENT THIS FUNCTION!
 * 
 * This function takes a single command string (no pipes) and parses
 * it into argc/argv format.
 * 
 * Steps:
 *   1. Copy cmd_line into cmd_buff->_cmd_buffer
 *   2. Split the string by spaces into tokens
 *   3. Store each token pointer in cmd_buff->argv[]
 *   4. Set cmd_buff->argc to number of tokens
 *   5. Ensure cmd_buff->argv[argc] is NULL (required for execvp later)
 * 
 * Example:
 *   Input:  "ls -la /tmp"
 *   Output: argc=3, argv=["ls", "-la", "/tmp", NULL]
 * 
 * Hints:
 *   - Use strcpy() to copy cmd_line to _cmd_buffer
 *   - Use strtok() to split by spaces
 *   - Remember to trim leading/trailing whitespace
 *   - Handle multiple consecutive spaces
 * 
 * @param cmd_line: Command string to parse
 * @param cmd_buff: Allocated cmd_buff_t to populate
 * @return: OK on success, error code on failure
 */
int build_cmd_buff(char *cmd_line, cmd_buff_t *cmd_buff)
{
    if (cmd_line == NULL || cmd_buff == NULL || cmd_buff->_cmd_buffer == NULL) {
        return ERR_MEMORY;
    }

    clear_cmd_buff(cmd_buff);

    // Copy into internal buffer
    strncpy(cmd_buff->_cmd_buffer, cmd_line, SH_CMD_MAX - 1);
    cmd_buff->_cmd_buffer[SH_CMD_MAX - 1] = '\0';

    // Trim leading/trailing whitespace in place
    char *start = cmd_buff->_cmd_buffer;
    while (*start && isspace((unsigned char)*start)) {
        start++;
    }
    if (*start == '\0') {
        return WARN_NO_CMDS;
    }
    char *end = start + strlen(start) - 1;
    while (end > start && isspace((unsigned char)*end)) {
        end--;
    }
    *(end + 1) = '\0';
    if (start != cmd_buff->_cmd_buffer) {
        memmove(cmd_buff->_cmd_buffer, start, strlen(start) + 1);
        start = cmd_buff->_cmd_buffer;
    }

    int argc = 0;
    char *token_start = NULL;
    char quote = '\0';
    char *dst = start;
    for (char *src = start;; src++) {
        char c = *src;
        if (quote) {
            if (c == quote) {
                quote = '\0';
            } else if (c == '\0') {
                *dst = '\0';
                if (token_start) {
                    if (argc >= CMD_ARGV_MAX - 1) {
                        return ERR_CMD_OR_ARGS_TOO_BIG;
                    }
                    cmd_buff->argv[argc++] = token_start;
                }
                break;
            } else {
                *dst++ = c;
            }
        } else {
            if (c == '"' || c == '\'') {
                quote = c;
                if (token_start == NULL) {
                    token_start = dst;
                }
            } else if (c == '\0' || isspace((unsigned char)c)) {
                if (token_start != NULL) {
                    *dst = '\0';
                    if (argc >= CMD_ARGV_MAX - 1) {
                        return ERR_CMD_OR_ARGS_TOO_BIG;
                    }
                    cmd_buff->argv[argc++] = token_start;
                    token_start = NULL;
                    dst++;
                }
                if (c == '\0') {
                    break;
                }
            } else {
                if (token_start == NULL) {
                    token_start = dst;
                }
                *dst++ = c;
            }
        }

        if ((size_t)(dst - cmd_buff->_cmd_buffer) >= SH_CMD_MAX - 1) {
            return ERR_CMD_OR_ARGS_TOO_BIG;
        }
    }

    if (argc == 0) {
        return WARN_NO_CMDS;
    }

    cmd_buff->argc = argc;
    cmd_buff->argv[argc] = NULL;

    return OK;
}

Built_In_Cmds exec_built_in_cmd(cmd_buff_t *cmd)
{
    Built_In_Cmds bi_cmd = match_command(cmd->argv[0]);
    
    switch (bi_cmd) {
        case BI_CMD_EXIT:
            // Exit is handled in main loop
            return BI_CMD_EXIT;
            
        case BI_RC:
            printf("%d\n", last_status);
            return BI_EXECUTED;
            
        case BI_CMD_CD:
            // CD will be implemented in Part 2
            if (cmd->argc==1) {
                last_status = 0;
                return BI_EXECUTED;
            }
            if (cmd->argc>2) {
                last_status = ERR_CMD_ARGS_BAD;
                return BI_EXECUTED;
            }
            if (chdir(cmd->argv[1]) != 0) {
                perror("cd");
                last_status = errno;
                return BI_EXECUTED;
            }
            last_status = 0;
            return BI_EXECUTED;
            
        default:
            return BI_NOT_BI;
    }
}



//===================================================================
// BUILT-IN COMMAND FUNCTIONS (PROVIDED FOR PART 1)
//===================================================================

/**
 * match_command - Check if input is a built-in command
 * 
 * This function is provided for you.
 */
Built_In_Cmds match_command(const char *input)
{
    if (strcmp(input, EXIT_CMD) == 0) {
        return BI_CMD_EXIT;
    }
    if (strcmp(input, "rc") == 0) {
        return BI_RC;
    }
    if (strcmp(input, "cd") == 0) {
        return BI_CMD_CD;
    }
    return BI_NOT_BI;
}



/*
 * Implement your exec_local_cmd_loop function by building a loop that prompts the 
 * user for input.  Use the SH_PROMPT constant from dshlib.h and then
 * use fgets to accept user input.
 * 
 *      while(1){
 *        printf("%s", SH_PROMPT);
 *        if (fgets(cmd_buff, ARG_MAX, stdin) == NULL){
 *           printf("\n");
 *           break;
 *        }
 *        //remove the trailing \n from cmd_buff
 *        cmd_buff[strcspn(cmd_buff,"\n")] = '\0';
 * 
 *        //IMPLEMENT THE REST OF THE REQUIREMENTS
 *      }
 * 
 *   Also, use the constants in the dshlib.h in this code.  
 *      SH_CMD_MAX              maximum buffer size for user input
 *      EXIT_CMD                constant that terminates the dsh program
 *      SH_PROMPT               the shell prompt
 *      OK                      the command was parsed properly
 *      WARN_NO_CMDS            the user command was empty
 *      ERR_TOO_MANY_COMMANDS   too many pipes used
 *      ERR_MEMORY              dynamic memory management failure
 * 
 *   errors returned
 *      OK                     No error
 *      ERR_MEMORY             Dynamic memory management failure
 *      WARN_NO_CMDS           No commands parsed
 *      ERR_TOO_MANY_COMMANDS  too many pipes used
 *   
 *   console messages
 *      CMD_WARN_NO_CMD        print on WARN_NO_CMDS
 *      CMD_ERR_PIPE_LIMIT     print on ERR_TOO_MANY_COMMANDS
 *      CMD_ERR_EXECUTE        print on execution failure of external command
 * 
 *  Standard Library Functions You Might Want To Consider Using (assignment 1+)
 *      malloc(), free(), strlen(), fgets(), strcspn(), printf()
 * 
 *  Standard Library Functions You Might Want To Consider Using (assignment 2+)
 *      fork(), execvp(), exit(), chdir()
 */
int exec_local_cmd_loop()
{
    char cmd_buff[SH_CMD_MAX];
    cmd_buff_t cmd;
    int rc = alloc_cmd_buff(&cmd);
    if (rc != OK) {
        return rc;
    }

    while (1) {
        printf("%s", SH_PROMPT);
        if (fgets(cmd_buff, SH_CMD_MAX, stdin) == NULL) {
            printf("\n");
            break;
        }
        cmd_buff[strcspn(cmd_buff, "\n")] = '\0';
        rc = build_cmd_buff(cmd_buff, &cmd);
        if (rc == WARN_NO_CMDS) {
            printf(CMD_WARN_NO_CMD);
            continue;
        } else if (rc != OK) {
            continue;
        }

        Built_In_Cmds bi = exec_built_in_cmd(&cmd);
        if (bi == BI_CMD_EXIT) {
            printf("exiting...\n");
            rc = OK;
            break;
        }
        if (bi == BI_EXECUTED) {
            continue; // built-in handled (e.g., cd/rc)
        }

        pid_t pid = fork();
        if (pid == 0) {
            execvp(cmd.argv[0], cmd.argv);
            _exit(errno); // return errno to parent
        } else if (pid > 0) {
            int status = 0;
            if (waitpid(pid, &status, 0) < 0) {
                perror("waitpid");
                rc = ERR_EXEC_CMD;
                last_status = errno;
                continue;
            }
            if (WIFEXITED(status)) {
                last_status = WEXITSTATUS(status);
            } else if (WIFSIGNALED(status)) {
                last_status = 128 + WTERMSIG(status);
            } else {
                last_status = ERR_EXEC_CMD;
            }

            if (last_status != 0) {
                switch (last_status) {
                    case ENOENT:
                        printf("Command not found in PATH\n");
                        break;
                    case EACCES:
                        printf("Permission denied\n");
                        break;
                    case ENOTDIR:
                        printf("Path component is not a directory\n");
                        break;
                    case ELOOP:
                        printf("Too many symbolic links encountered\n");
                        break;
                    case ENAMETOOLONG:
                        printf("Command name too long\n");
                        break;
                    default:
                        printf(CMD_ERR_EXECUTE);
                        break;
                }
            }
        } else {
            perror("fork");
            rc = ERR_EXEC_CMD;
            last_status = errno;
        }
    }

    free_cmd_buff(&cmd);
    return rc;
}

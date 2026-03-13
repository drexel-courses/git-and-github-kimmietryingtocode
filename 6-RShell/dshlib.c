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

#ifndef CMD_ERR_EXECUTE
#define CMD_ERR_EXECUTE "error: execution failure\n"
#endif

static int last_status = 0;

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
    cmd_buff->input_file = NULL;
    cmd_buff->output_file = NULL;
    cmd_buff->append_mode = false;
    return OK;
}

int free_cmd_buff(cmd_buff_t *cmd_buff)
{
    if (cmd_buff->_cmd_buffer != NULL) {
        free(cmd_buff->_cmd_buffer);
        cmd_buff->_cmd_buffer = NULL;
    }
    cmd_buff->argc = 0;
    return OK;
}

int clear_cmd_buff(cmd_buff_t *cmd_buff)
{
    cmd_buff->argc = 0;
    for (int i = 0; i < CMD_ARGV_MAX; i++) {
        cmd_buff->argv[i] = NULL;
    }
    cmd_buff->input_file = NULL;
    cmd_buff->output_file = NULL;
    cmd_buff->append_mode = false;
    return OK;
}

int close_cmd_buff(cmd_buff_t *cmd_buff)
{
    return free_cmd_buff(cmd_buff);
}

int free_cmd_list(command_list_t *cmd_lst)
{
    if (cmd_lst == NULL) {
        return ERR_MEMORY;
    }
    for (int i = 0; i < cmd_lst->num; i++) {
        free_cmd_buff(&cmd_lst->commands[i]);
    }
    cmd_lst->num = 0;
    return OK;
}

int build_cmd_buff(char *cmd_line, cmd_buff_t *cmd_buff)
{
    if (cmd_line == NULL || cmd_buff == NULL || cmd_buff->_cmd_buffer == NULL) {
        return ERR_MEMORY;
    }

    clear_cmd_buff(cmd_buff);

    strncpy(cmd_buff->_cmd_buffer, cmd_line, SH_CMD_MAX - 1);
    cmd_buff->_cmd_buffer[SH_CMD_MAX - 1] = '\0';

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

    char expanded[SH_CMD_MAX * 2];
    size_t exp_idx = 0;
    char quote = '\0';

    for (char *src = start;; src++) {
        char c = *src;

        if (quote) {
            if (c == quote) {
                quote = '\0';
            }
            if (exp_idx >= sizeof(expanded) - 1) {
                return ERR_CMD_OR_ARGS_TOO_BIG;
            }
            expanded[exp_idx++] = c;
            if (c == '\0') {
                break;
            }
            continue;
        }

        if (c == '"' || c == '\'') {
            quote = c;
            if (exp_idx >= sizeof(expanded) - 1) {
                return ERR_CMD_OR_ARGS_TOO_BIG;
            }
            expanded[exp_idx++] = c;
            continue;
        }

        if (c == '<' || c == '>') {
            if (exp_idx >= sizeof(expanded) - 4) {
                return ERR_CMD_OR_ARGS_TOO_BIG;
            }
            expanded[exp_idx++] = ' ';
            expanded[exp_idx++] = c;
            if (c == '>' && *(src + 1) == '>') {
                expanded[exp_idx++] = '>';
                src++;
            }
            expanded[exp_idx++] = ' ';
            continue;
        }

        if (exp_idx >= sizeof(expanded) - 1) {
            return ERR_CMD_OR_ARGS_TOO_BIG;
        }
        expanded[exp_idx++] = c;
        if (c == '\0') {
            break;
        }
    }

    int tokc = 0;
    char *tokens[CMD_ARGV_MAX * 2];
    char *dst = cmd_buff->_cmd_buffer;
    char *token_start = NULL;
    quote = '\0';

    for (char *src = expanded;; src++) {
        char c = *src;

        if (quote) {
            if (c == quote) {
                quote = '\0';
            } else if (c == '\0') {
                *dst = '\0';
                if (token_start) {
                    if (tokc >= CMD_ARGV_MAX * 2) {
                        return ERR_CMD_OR_ARGS_TOO_BIG;
                    }
                    tokens[tokc++] = token_start;
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
                    if (tokc >= CMD_ARGV_MAX * 2) {
                        return ERR_CMD_OR_ARGS_TOO_BIG;
                    }
                    tokens[tokc++] = token_start;
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

    int argc = 0;
    for (int i = 0; i < tokc; i++) {
        if (strcmp(tokens[i], "<") == 0) {
            if (i + 1 >= tokc) {
                return ERR_CMD_ARGS_BAD;
            }
            i++;
            if (strcmp(tokens[i], "<") == 0 || strcmp(tokens[i], ">") == 0 || strcmp(tokens[i], ">>") == 0) {
                return ERR_CMD_ARGS_BAD;
            }
            cmd_buff->input_file = tokens[i];
            continue;
        }

        if (strcmp(tokens[i], ">") == 0 || strcmp(tokens[i], ">>") == 0) {
            bool append = (strcmp(tokens[i], ">>") == 0);
            if (i + 1 >= tokc) {
                return ERR_CMD_ARGS_BAD;
            }
            i++;
            if (strcmp(tokens[i], "<") == 0 || strcmp(tokens[i], ">") == 0 || strcmp(tokens[i], ">>") == 0) {
                return ERR_CMD_ARGS_BAD;
            }
            cmd_buff->output_file = tokens[i];
            cmd_buff->append_mode = append;
            continue;
        }

        if (argc >= CMD_ARGV_MAX - 1) {
            return ERR_CMD_OR_ARGS_TOO_BIG;
        }
        cmd_buff->argv[argc++] = tokens[i];
    }

    if (argc == 0) {
        return WARN_NO_CMDS;
    }

    cmd_buff->argc = argc;
    cmd_buff->argv[argc] = NULL;
    return OK;
}

int build_cmd_list(char *cmd_line, command_list_t *clist)
{
    if (cmd_line == NULL || clist == NULL) {
        return ERR_MEMORY;
    }

    clist->num = 0;

    char line_copy[SH_CMD_MAX];
    strncpy(line_copy, cmd_line, SH_CMD_MAX - 1);
    line_copy[SH_CMD_MAX - 1] = '\0';

    char *start = line_copy;
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

    if (start != line_copy) {
        memmove(line_copy, start, strlen(start) + 1);
    }

    int pipe_count = 0;
    for (char *p = line_copy; *p; p++) {
        if (*p == PIPE_CHAR) {
            pipe_count++;
        }
    }
    if (pipe_count + 1 > CMD_MAX) {
        return ERR_TOO_MANY_COMMANDS;
    }

    char *saveptr = NULL;
    char *segment = strtok_r(line_copy, PIPE_STRING, &saveptr);

    while (segment != NULL) {
        char *seg_start = segment;
        while (*seg_start && isspace((unsigned char)*seg_start)) {
            seg_start++;
        }
        if (*seg_start == '\0') {
            free_cmd_list(clist);
            return WARN_NO_CMDS;
        }

        char *seg_end = seg_start + strlen(seg_start) - 1;
        while (seg_end > seg_start && isspace((unsigned char)*seg_end)) {
            seg_end--;
        }
        *(seg_end + 1) = '\0';

        int rc = alloc_cmd_buff(&clist->commands[clist->num]);
        if (rc != OK) {
            free_cmd_list(clist);
            return rc;
        }

        rc = build_cmd_buff(seg_start, &clist->commands[clist->num]);
        if (rc != OK) {
            free_cmd_list(clist);
            return rc;
        }

        clist->num++;
        segment = strtok_r(NULL, PIPE_STRING, &saveptr);
    }

    if (clist->num == 0) {
        return WARN_NO_CMDS;
    }

    return OK;
}

Built_In_Cmds match_command(const char *input)
{
    if (strcmp(input, EXIT_CMD) == 0) {
        return BI_CMD_EXIT;
    }
    if (strcmp(input, "dragon") == 0) {
        return BI_CMD_DRAGON;
    }
    if (strcmp(input, "cd") == 0) {
        return BI_CMD_CD;
    }
    if (strcmp(input, "rc") == 0) {
        return BI_CMD_RC;
    }
    if (strcmp(input, "stop-server") == 0) {
        return BI_CMD_STOP_SVR;
    }
    return BI_NOT_BI;
}

Built_In_Cmds exec_built_in_cmd(cmd_buff_t *cmd)
{
    if (cmd == NULL || cmd->argc == 0 || cmd->argv[0] == NULL) {
        return BI_NOT_BI;
    }

    Built_In_Cmds bi_cmd = match_command(cmd->argv[0]);

    switch (bi_cmd) {
    case BI_CMD_EXIT:
    case BI_CMD_STOP_SVR:
        return bi_cmd;

    case BI_CMD_RC:
        printf("%d\n", last_status);
        return BI_EXECUTED;

    case BI_CMD_CD:
        if (cmd->argc == 1) {
            last_status = 0;
            return BI_EXECUTED;
        }
        if (cmd->argc > 2) {
            last_status = ERR_CMD_ARGS_BAD;
            return BI_EXECUTED;
        }
        if (chdir(cmd->argv[1]) != 0) {
            perror("cd");
            last_status = errno;
        } else {
            last_status = 0;
        }
        return BI_EXECUTED;

    case BI_CMD_DRAGON:
        printf("%s\n", BI_NOT_IMPLEMENTED);
        last_status = 0;
        return BI_EXECUTED;

    default:
        return BI_NOT_BI;
    }
}

int exec_cmd(cmd_buff_t *cmd)
{
    if (cmd == NULL || cmd->argc == 0 || cmd->argv[0] == NULL) {
        return WARN_NO_CMDS;
    }

    pid_t pid = fork();
    if (pid < 0) {
        last_status = errno;
        return ERR_EXEC_CMD;
    }

    if (pid == 0) {
        if (cmd->input_file != NULL) {
            int in_fd = open(cmd->input_file, O_RDONLY);
            if (in_fd < 0 || dup2(in_fd, STDIN_FILENO) < 0) {
                if (in_fd >= 0) {
                    close(in_fd);
                }
                write(STDERR_FILENO, CMD_ERR_EXECUTE, strlen(CMD_ERR_EXECUTE));
                _exit(127);
            }
            close(in_fd);
        }

        if (cmd->output_file != NULL) {
            int flags = O_WRONLY | O_CREAT | (cmd->append_mode ? O_APPEND : O_TRUNC);
            int out_fd = open(cmd->output_file, flags, 0666);
            if (out_fd < 0 || dup2(out_fd, STDOUT_FILENO) < 0) {
                if (out_fd >= 0) {
                    close(out_fd);
                }
                write(STDERR_FILENO, CMD_ERR_EXECUTE, strlen(CMD_ERR_EXECUTE));
                _exit(127);
            }
            close(out_fd);
        }

        execvp(cmd->argv[0], cmd->argv);
        write(STDERR_FILENO, CMD_ERR_EXECUTE, strlen(CMD_ERR_EXECUTE));
        _exit(127);
    }

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        last_status = errno;
        return ERR_EXEC_CMD;
    }

    if (WIFEXITED(status)) {
        last_status = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        last_status = 128 + WTERMSIG(status);
    } else {
        last_status = ERR_EXEC_CMD;
    }

    return OK;
}

int execute_pipeline(command_list_t *clist)
{
    if (clist == NULL || clist->num <= 0) {
        return WARN_NO_CMDS;
    }

    if (clist->num == 1) {
        return exec_cmd(&clist->commands[0]);
    }

    int pipefds[CMD_MAX - 1][2];
    pid_t pids[CMD_MAX];

    for (int i = 0; i < clist->num - 1; i++) {
        if (pipe(pipefds[i]) < 0) {
            last_status = errno;
            return ERR_EXEC_CMD;
        }
    }

    for (int i = 0; i < clist->num; i++) {
        pids[i] = fork();
        if (pids[i] < 0) {
            last_status = errno;
            for (int j = 0; j < clist->num - 1; j++) {
                close(pipefds[j][0]);
                close(pipefds[j][1]);
            }
            return ERR_EXEC_CMD;
        }

        if (pids[i] == 0) {
            if (i > 0) {
                if (dup2(pipefds[i - 1][0], STDIN_FILENO) < 0) {
                    write(STDERR_FILENO, CMD_ERR_EXECUTE, strlen(CMD_ERR_EXECUTE));
                    _exit(127);
                }
            }
            if (i < clist->num - 1) {
                if (dup2(pipefds[i][1], STDOUT_FILENO) < 0) {
                    write(STDERR_FILENO, CMD_ERR_EXECUTE, strlen(CMD_ERR_EXECUTE));
                    _exit(127);
                }
            }

            if (clist->commands[i].input_file != NULL) {
                int in_fd = open(clist->commands[i].input_file, O_RDONLY);
                if (in_fd < 0 || dup2(in_fd, STDIN_FILENO) < 0) {
                    if (in_fd >= 0) {
                        close(in_fd);
                    }
                    write(STDERR_FILENO, CMD_ERR_EXECUTE, strlen(CMD_ERR_EXECUTE));
                    _exit(127);
                }
                close(in_fd);
            }

            if (clist->commands[i].output_file != NULL) {
                int flags = O_WRONLY | O_CREAT | (clist->commands[i].append_mode ? O_APPEND : O_TRUNC);
                int out_fd = open(clist->commands[i].output_file, flags, 0666);
                if (out_fd < 0 || dup2(out_fd, STDOUT_FILENO) < 0) {
                    if (out_fd >= 0) {
                        close(out_fd);
                    }
                    write(STDERR_FILENO, CMD_ERR_EXECUTE, strlen(CMD_ERR_EXECUTE));
                    _exit(127);
                }
                close(out_fd);
            }

            for (int j = 0; j < clist->num - 1; j++) {
                close(pipefds[j][0]);
                close(pipefds[j][1]);
            }

            execvp(clist->commands[i].argv[0], clist->commands[i].argv);
            write(STDERR_FILENO, CMD_ERR_EXECUTE, strlen(CMD_ERR_EXECUTE));
            _exit(127);
        }
    }

    for (int i = 0; i < clist->num - 1; i++) {
        close(pipefds[i][0]);
        close(pipefds[i][1]);
    }

    int final_status = 0;
    for (int i = 0; i < clist->num; i++) {
        int status = 0;
        if (waitpid(pids[i], &status, 0) < 0) {
            last_status = errno;
            return ERR_EXEC_CMD;
        }
        if (i == clist->num - 1) {
            final_status = status;
        }
    }

    if (WIFEXITED(final_status)) {
        last_status = WEXITSTATUS(final_status);
    } else if (WIFSIGNALED(final_status)) {
        last_status = 128 + WTERMSIG(final_status);
    } else {
        last_status = ERR_EXEC_CMD;
    }

    return OK;
}

int exec_local_cmd_loop()
{
    char cmd_line[SH_CMD_MAX];
    command_list_t clist;
    int rc = OK;

    while (1) {
        printf("%s", SH_PROMPT);
        if (fgets(cmd_line, SH_CMD_MAX, stdin) == NULL) {
            printf("\n");
            break;
        }

        cmd_line[strcspn(cmd_line, "\n")] = '\0';

        rc = build_cmd_list(cmd_line, &clist);
        if (rc == WARN_NO_CMDS) {
            printf(CMD_WARN_NO_CMD);
            continue;
        }
        if (rc == ERR_TOO_MANY_COMMANDS) {
            printf(CMD_ERR_PIPE_LIMIT, CMD_MAX);
            continue;
        }
        if (rc != OK) {
            continue;
        }

        if (clist.num == 1) {
            Built_In_Cmds bi = exec_built_in_cmd(&clist.commands[0]);
            if (bi == BI_CMD_EXIT) {
                free_cmd_list(&clist);
                return OK;
            }
            if (bi == BI_EXECUTED || bi == BI_CMD_STOP_SVR) {
                free_cmd_list(&clist);
                continue;
            }
        }

        int exec_rc = execute_pipeline(&clist);
        if (exec_rc != OK) {
            printf(CMD_ERR_EXECUTE);
            rc = exec_rc;
        }

        free_cmd_list(&clist);
    }

    return rc;
}

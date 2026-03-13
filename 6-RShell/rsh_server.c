#include <sys/socket.h>
#include <sys/wait.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>

#include "dshlib.h"
#include "rshlib.h"

#ifndef CMD_ERR_EXECUTE
#define CMD_ERR_EXECUTE "error: execution failure\n"
#endif

typedef struct client_thread_args
{
    int main_socket;
    int cli_socket;
} client_thread_args_t;

static int threaded_mode_enabled = 0;
static int threaded_stop_requested = 0;
static pthread_mutex_t threaded_state_lock = PTHREAD_MUTEX_INITIALIZER;

static int process_cli_requests_threaded(int svr_socket);
static int is_threaded_stop_requested(void);
static void request_threaded_server_stop(int main_socket);

static int send_all(int sock, const char *buf, size_t len)
{
    size_t total = 0;
    while (total < len) {
        ssize_t sent = send(sock, buf + total, len - total, 0);
        if (sent <= 0) {
            return ERR_RDSH_COMMUNICATION;
        }
        total += (size_t)sent;
    }
    return OK;
}

static ssize_t recv_until_null(int sock, char *buf, size_t buf_sz)
{
    size_t total = 0;

    while (1) {
        if (total >= buf_sz) {
            return -1;
        }

        ssize_t n = recv(sock, buf + total, buf_sz - total, 0);
        if (n < 0) {
            return -1;
        }
        if (n == 0) {
            return -2;
        }

        char *found = memchr(buf + total, '\0', (size_t)n);
        if (found != NULL) {
            size_t cmd_len = (size_t)(found - buf);
            buf[cmd_len] = '\0';
            return (ssize_t)cmd_len;
        }

        total += (size_t)n;
    }
}

int start_server(char *ifaces, int port, int is_threaded)
{
    int svr_socket;
    int rc;

    set_threaded_server(is_threaded);

    svr_socket = boot_server(ifaces, port);
    if (svr_socket < 0) {
        return svr_socket;
    }

    if (threaded_mode_enabled) {
        rc = process_cli_requests_threaded(svr_socket);
        if (rc != OK_EXIT || !is_threaded_stop_requested()) {
            stop_server(svr_socket);
        }
    } else {
        rc = process_cli_requests(svr_socket);
        stop_server(svr_socket);
    }

    return rc;
}

int stop_server(int svr_socket)
{
    return close(svr_socket);
}

int boot_server(char *ifaces, int port)
{
    int svr_socket;
    int ret;
    int enable = 1;
    struct sockaddr_in addr;

    svr_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (svr_socket < 0) {
        perror("socket");
        return ERR_RDSH_COMMUNICATION;
    }

    ret = setsockopt(svr_socket, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));
    if (ret < 0) {
        perror("setsockopt");
        close(svr_socket);
        return ERR_RDSH_COMMUNICATION;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);

    if (inet_pton(AF_INET, ifaces, &addr.sin_addr) <= 0) {
        perror("inet_pton");
        close(svr_socket);
        return ERR_RDSH_COMMUNICATION;
    }

    ret = bind(svr_socket, (struct sockaddr *)&addr, sizeof(addr));
    if (ret < 0) {
        perror("bind");
        close(svr_socket);
        return ERR_RDSH_COMMUNICATION;
    }

    ret = listen(svr_socket, 20);
    if (ret < 0) {
        perror("listen");
        close(svr_socket);
        return ERR_RDSH_COMMUNICATION;
    }

    return svr_socket;
}

int process_cli_requests(int svr_socket)
{
    int cli_socket;
    int rc = OK;

    while (1) {
        cli_socket = accept(svr_socket, NULL, NULL);
        if (cli_socket < 0) {
            perror("accept");
            return ERR_RDSH_COMMUNICATION;
        }

        rc = exec_client_requests(cli_socket);
        close(cli_socket);

        if (rc == OK_EXIT) {
            return OK_EXIT;
        }
        if (rc < 0) {
            return rc;
        }
    }
}

static int process_cli_requests_threaded(int svr_socket)
{
    while (!is_threaded_stop_requested()) {
        int cli_socket = accept(svr_socket, NULL, NULL);
        if (cli_socket < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (is_threaded_stop_requested() && (errno == EBADF || errno == EINVAL)) {
                return OK_EXIT;
            }
            perror("accept");
            return ERR_RDSH_COMMUNICATION;
        }

        int rc = exec_client_thread(svr_socket, cli_socket);
        if (rc < 0) {
            return rc;
        }
    }

    return OK_EXIT;
}

int exec_client_requests(int cli_socket)
{
    command_list_t cmd_list;
    int cmd_rc;
    int last_rc = 0;
    char *io_buff = malloc(RDSH_COMM_BUFF_SZ);

    if (io_buff == NULL) {
        return ERR_RDSH_SERVER;
    }

    while (1) {
        ssize_t io_size = recv_until_null(cli_socket, io_buff, RDSH_COMM_BUFF_SZ);
        if (io_size == -2) {
            free(io_buff);
            return OK;
        }
        if (io_size < 0) {
            free(io_buff);
            return ERR_RDSH_COMMUNICATION;
        }

        cmd_rc = build_cmd_list(io_buff, &cmd_list);
        if (cmd_rc == WARN_NO_CMDS) {
            if (send_message_string(cli_socket, CMD_WARN_NO_CMD) != OK) {
                free(io_buff);
                return ERR_RDSH_COMMUNICATION;
            }
            continue;
        }
        if (cmd_rc == ERR_TOO_MANY_COMMANDS) {
            char msg[128];
            snprintf(msg, sizeof(msg), CMD_ERR_PIPE_LIMIT, CMD_MAX);
            if (send_message_string(cli_socket, msg) != OK) {
                free(io_buff);
                return ERR_RDSH_COMMUNICATION;
            }
            continue;
        }
        if (cmd_rc != OK) {
            if (send_message_string(cli_socket, CMD_ERR_RDSH_EXEC) != OK) {
                free(io_buff);
                return ERR_RDSH_COMMUNICATION;
            }
            continue;
        }

        if (cmd_list.num == 1) {
            Built_In_Cmds bi = rsh_built_in_cmd(&cmd_list.commands[0]);

            if (bi == BI_CMD_EXIT) {
                free_cmd_list(&cmd_list);
                send_message_eof(cli_socket);
                free(io_buff);
                return OK;
            }
            if (bi == BI_CMD_STOP_SVR) {
                free_cmd_list(&cmd_list);
                send_message_eof(cli_socket);
                free(io_buff);
                return OK_EXIT;
            }
            if (bi == BI_CMD_RC) {
                char msg[128];
                snprintf(msg, sizeof(msg), RCMD_MSG_SVR_RC_CMD, last_rc);
                free_cmd_list(&cmd_list);
                if (send_message_string(cli_socket, msg) != OK) {
                    free(io_buff);
                    return ERR_RDSH_COMMUNICATION;
                }
                continue;
            }
            if (bi == BI_EXECUTED) {
                free_cmd_list(&cmd_list);
                if (send_message_eof(cli_socket) != OK) {
                    free(io_buff);
                    return ERR_RDSH_COMMUNICATION;
                }
                last_rc = 0;
                continue;
            }
        }

        cmd_rc = rsh_execute_pipeline(cli_socket, &cmd_list);
        free_cmd_list(&cmd_list);

        if (cmd_rc < 0) {
            if (send_message_string(cli_socket, CMD_ERR_RDSH_EXEC) != OK) {
                free(io_buff);
                return ERR_RDSH_COMMUNICATION;
            }
            last_rc = cmd_rc;
            continue;
        }

        last_rc = cmd_rc;
        if (send_message_eof(cli_socket) != OK) {
            free(io_buff);
            return ERR_RDSH_COMMUNICATION;
        }
    }
}

int send_message_eof(int cli_socket)
{
    int send_len = (int)sizeof(RDSH_EOF_CHAR);
    int sent_len = send(cli_socket, &RDSH_EOF_CHAR, send_len, 0);

    if (sent_len != send_len) {
        return ERR_RDSH_COMMUNICATION;
    }
    return OK;
}

int send_message_string(int cli_socket, char *buff)
{
    size_t len = (buff == NULL) ? 0 : strlen(buff);

    if (len > 0) {
        if (send_all(cli_socket, buff, len) != OK) {
            return ERR_RDSH_COMMUNICATION;
        }
    }

    return send_message_eof(cli_socket);
}

int rsh_execute_pipeline(int cli_sock, command_list_t *clist)
{
    if (clist == NULL || clist->num <= 0) {
        return ERR_RDSH_CMD_EXEC;
    }

    int pipes[CMD_MAX - 1][2];
    pid_t pids[CMD_MAX];
    int statuses[CMD_MAX];

    for (int i = 0; i < clist->num - 1; i++) {
        if (pipe(pipes[i]) < 0) {
            return ERR_RDSH_CMD_EXEC;
        }
    }

    for (int i = 0; i < clist->num; i++) {
        pids[i] = fork();
        if (pids[i] < 0) {
            for (int j = 0; j < clist->num - 1; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }
            return ERR_RDSH_CMD_EXEC;
        }

        if (pids[i] == 0) {
            if (i == 0) {
                if (dup2(cli_sock, STDIN_FILENO) < 0) {
                    write(STDERR_FILENO, CMD_ERR_EXECUTE, strlen(CMD_ERR_EXECUTE));
                    _exit(127);
                }
            } else {
                if (dup2(pipes[i - 1][0], STDIN_FILENO) < 0) {
                    write(STDERR_FILENO, CMD_ERR_EXECUTE, strlen(CMD_ERR_EXECUTE));
                    _exit(127);
                }
            }

            if (i == clist->num - 1) {
                if (dup2(cli_sock, STDOUT_FILENO) < 0 || dup2(cli_sock, STDERR_FILENO) < 0) {
                    write(STDERR_FILENO, CMD_ERR_EXECUTE, strlen(CMD_ERR_EXECUTE));
                    _exit(127);
                }
            } else {
                if (dup2(pipes[i][1], STDOUT_FILENO) < 0) {
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
                close(pipes[j][0]);
                close(pipes[j][1]);
            }

            execvp(clist->commands[i].argv[0], clist->commands[i].argv);
            write(STDERR_FILENO, CMD_ERR_EXECUTE, strlen(CMD_ERR_EXECUTE));
            _exit(127);
        }
    }

    for (int i = 0; i < clist->num - 1; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }

    for (int i = 0; i < clist->num; i++) {
        if (waitpid(pids[i], &statuses[i], 0) < 0) {
            return ERR_RDSH_CMD_EXEC;
        }
    }

    int exit_code = 0;
    if (WIFEXITED(statuses[clist->num - 1])) {
        exit_code = WEXITSTATUS(statuses[clist->num - 1]);
    } else if (WIFSIGNALED(statuses[clist->num - 1])) {
        exit_code = 128 + WTERMSIG(statuses[clist->num - 1]);
    } else {
        exit_code = ERR_RDSH_CMD_EXEC;
    }

    for (int i = 0; i < clist->num; i++) {
        if (WIFEXITED(statuses[i]) && WEXITSTATUS(statuses[i]) == EXIT_SC) {
            exit_code = EXIT_SC;
        }
    }

    return exit_code;
}

Built_In_Cmds rsh_match_command(const char *input)
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
    if (strcmp(input, "stop-server") == 0) {
        return BI_CMD_STOP_SVR;
    }
    if (strcmp(input, "rc") == 0) {
        return BI_CMD_RC;
    }
    return BI_NOT_BI;
}

Built_In_Cmds rsh_built_in_cmd(cmd_buff_t *cmd)
{
    if (cmd == NULL || cmd->argc == 0 || cmd->argv[0] == NULL) {
        return BI_NOT_BI;
    }

    Built_In_Cmds ctype = rsh_match_command(cmd->argv[0]);

    switch (ctype) {
    case BI_CMD_EXIT:
        return BI_CMD_EXIT;

    case BI_CMD_STOP_SVR:
        return BI_CMD_STOP_SVR;

    case BI_CMD_RC:
        return BI_CMD_RC;

    case BI_CMD_CD:
        if (cmd->argc == 2) {
            if (chdir(cmd->argv[1]) != 0) {
                return BI_EXECUTED;
            }
            return BI_EXECUTED;
        }
        if (cmd->argc == 1) {
            return BI_EXECUTED;
        }
        return BI_EXECUTED;

    case BI_CMD_DRAGON:
        return BI_EXECUTED;

    default:
        return BI_NOT_BI;
    }
}

void set_threaded_server(int val)
{
    pthread_mutex_lock(&threaded_state_lock);
    threaded_mode_enabled = (val != 0);
    threaded_stop_requested = 0;
    pthread_mutex_unlock(&threaded_state_lock);
}

int exec_client_thread(int main_socket, int cli_socket)
{
    pthread_t tid;
    client_thread_args_t *args = malloc(sizeof(client_thread_args_t));
    if (args == NULL) {
        close(cli_socket);
        return ERR_MEMORY;
    }

    args->main_socket = main_socket;
    args->cli_socket = cli_socket;

    if (pthread_create(&tid, NULL, handle_client, args) != 0) {
        free(args);
        close(cli_socket);
        return ERR_RDSH_SERVER;
    }

    if (pthread_detach(tid) != 0) {
        pthread_cancel(tid);
        pthread_join(tid, NULL);
        close(cli_socket);
        return ERR_RDSH_SERVER;
    }

    return OK;
}

void *handle_client(void *arg)
{
    client_thread_args_t *args = (client_thread_args_t *)arg;
    if (args == NULL) {
        return NULL;
    }

    int main_socket = args->main_socket;
    int cli_socket = args->cli_socket;
    free(args);

    int rc = exec_client_requests(cli_socket);
    close(cli_socket);

    if (rc == OK_EXIT) {
        request_threaded_server_stop(main_socket);
    }

    return NULL;
}

static int is_threaded_stop_requested(void)
{
    int requested;
    pthread_mutex_lock(&threaded_state_lock);
    requested = threaded_stop_requested;
    pthread_mutex_unlock(&threaded_state_lock);
    return requested;
}

static void request_threaded_server_stop(int main_socket)
{
    pthread_mutex_lock(&threaded_state_lock);
    if (!threaded_stop_requested) {
        threaded_stop_requested = 1;
        shutdown(main_socket, SHUT_RDWR);
        close(main_socket);
    }
    pthread_mutex_unlock(&threaded_state_lock);
}

#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "dshlib.h"
#include "rshlib.h"

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

int exec_remote_cmd_loop(char *address, int port)
{
    char *cmd_buff = malloc(SH_CMD_MAX);
    char *rsp_buff = malloc(RDSH_COMM_BUFF_SZ);
    int cli_socket;

    if (cmd_buff == NULL || rsp_buff == NULL) {
        return client_cleanup(-1, cmd_buff, rsp_buff, ERR_MEMORY);
    }

    cli_socket = start_client(address, port);
    if (cli_socket < 0) {
        perror("start_client");
        return client_cleanup(cli_socket, cmd_buff, rsp_buff, ERR_RDSH_CLIENT);
    }

    while (1) {
        printf("%s", SH_PROMPT);
        if (fgets(cmd_buff, SH_CMD_MAX, stdin) == NULL) {
            printf("\n");
            break;
        }

        cmd_buff[strcspn(cmd_buff, "\n")] = '\0';

        size_t cmd_len = strlen(cmd_buff) + 1;
        if (send_all(cli_socket, cmd_buff, cmd_len) != OK) {
            return client_cleanup(cli_socket, cmd_buff, rsp_buff, ERR_RDSH_COMMUNICATION);
        }

        while (1) {
            ssize_t recv_len = recv(cli_socket, rsp_buff, RDSH_COMM_BUFF_SZ, 0);
            if (recv_len < 0) {
                return client_cleanup(cli_socket, cmd_buff, rsp_buff, ERR_RDSH_COMMUNICATION);
            }
            if (recv_len == 0) {
                printf("%s", RCMD_SERVER_EXITED);
                return client_cleanup(cli_socket, cmd_buff, rsp_buff, OK);
            }

            int is_last = (rsp_buff[recv_len - 1] == RDSH_EOF_CHAR);
            if (is_last) {
                recv_len--;
            }

            if (recv_len > 0) {
                printf("%.*s", (int)recv_len, rsp_buff);
            }

            if (is_last) {
                break;
            }
        }

        if (strcmp(cmd_buff, EXIT_CMD) == 0 || strcmp(cmd_buff, "stop-server") == 0) {
            break;
        }
    }

    return client_cleanup(cli_socket, cmd_buff, rsp_buff, OK);
}

int start_client(char *server_ip, int port)
{
    struct sockaddr_in addr;
    int cli_socket;

    cli_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (cli_socket < 0) {
        return ERR_RDSH_CLIENT;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);

    if (inet_pton(AF_INET, server_ip, &addr.sin_addr) <= 0) {
        close(cli_socket);
        return ERR_RDSH_CLIENT;
    }

    if (connect(cli_socket, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(cli_socket);
        return ERR_RDSH_CLIENT;
    }

    return cli_socket;
}

int client_cleanup(int cli_socket, char *cmd_buff, char *rsp_buff, int rc)
{
    if (cli_socket > 0) {
        close(cli_socket);
    }

    free(cmd_buff);
    free(rsp_buff);

    return rc;
}

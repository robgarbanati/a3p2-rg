#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#define SERVER_PORT 9000
#define DATA_FILE   "/var/tmp/aesdsocketdata"
#define RECV_CHUNK  1024

int main(void) {
    int server_socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket_fd == -1) {
        perror("socket");
        return -1;
    }

    int optval = 1;
    setsockopt(server_socket_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    struct sockaddr_in server_addr = {0};
    server_addr.sin_family      = AF_INET;
    server_addr.sin_port        = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(server_socket_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("bind");
        close(server_socket_fd);
        return -1;
    }

    if (listen(server_socket_fd, 5) == -1) {
        perror("listen");
        close(server_socket_fd);
        return -1;
    }

    openlog("aesdsocket", LOG_PID, LOG_USER);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_addrlen = sizeof(client_addr);
        int connfd = accept(server_socket_fd, (struct sockaddr *)&client_addr, &client_addrlen);
        if (connfd == -1) {
            perror("accept");
            break;
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        syslog(LOG_DEBUG, "Accepted connection from %s", client_ip);

        FILE *f = fopen(DATA_FILE, "a");
        if (f == NULL) {
            perror("fopen");
            close(connfd);
            break;
        }

        char *packet = NULL;
        size_t packet_len = 0;
        char chunk[RECV_CHUNK];
        ssize_t n;

        while ((n = recv(connfd, chunk, sizeof(chunk), 0)) > 0) {
            char *tmp = realloc(packet, packet_len + n + 1);
            if (tmp == NULL) {
                syslog(LOG_ERR, "realloc failed, discarding packet");
                free(packet);
                packet = NULL;
                packet_len = 0;
                continue;
            }
            packet = tmp;
            memcpy(packet + packet_len, chunk, n);
            packet_len += n;
            packet[packet_len] = '\0';

            char *start = packet;
            char *nl;
            while ((nl = strchr(start, '\n')) != NULL) {
                fwrite(start, 1, nl - start + 1, f);
                start = nl + 1;
            }

            size_t remaining = packet_len - (start - packet);
            memmove(packet, start, remaining);
            packet_len = remaining;
        }

        free(packet);
        fclose(f);
        close(connfd);
    }

    close(server_socket_fd);
    return 0;
}

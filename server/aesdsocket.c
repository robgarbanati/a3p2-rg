#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <sys/stat.h>
#include <unistd.h>

#define SERVER_PORT 9000
#define DATA_FILE   "/var/tmp/aesdsocketdata"
#define RECV_CHUNK  1024

static volatile sig_atomic_t caught_signal = 0;

static void signal_handler(int sig) {
    (void)sig;
    caught_signal = 1;
}

int main(int argc, char *argv[]) {
    int daemon_mode = (argc == 2 && strcmp(argv[1], "-d") == 0);
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    openlog("aesdsocket", LOG_PID, LOG_USER);

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

    if (daemon_mode) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            close(server_socket_fd);
            return -1;
        }
        if (pid > 0)
            return 0;  // parent exits, shell gets control back
        // child continues as daemon
        setsid();
    }

    if (listen(server_socket_fd, 5) == -1) {
        perror("listen");
        close(server_socket_fd);
        return -1;
    }

    // Opened once; persists across all connections so packets accumulate.
    int data_fd = open(DATA_FILE, O_RDWR | O_CREAT | O_APPEND, 0644);
    if (data_fd == -1) {
        perror("open");
        close(server_socket_fd);
        return -1;
    }

    while (!caught_signal) {
        struct sockaddr_in client_addr;
        socklen_t client_addrlen = sizeof(client_addr);
        int connfd = accept(server_socket_fd, (struct sockaddr *)&client_addr, &client_addrlen);
        if (connfd == -1)
            break;

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        syslog(LOG_DEBUG, "Accepted connection from %s", client_ip);

        // ptr to beginning of the packet
        char *packet = NULL;
        // keep track of the size of the packet so far. Basically this is the length of 
        // allocated data so far.
        size_t packet_len = 0;
        // Stack buffer to get portions of packets in reasonable sizes.
        char chunk[RECV_CHUNK];
        // rcvd_data_len
        ssize_t rcvd_data_len;

        while ((rcvd_data_len = recv(connfd, chunk, sizeof(chunk), 0)) > 0) {
            char *tmp = realloc(packet, packet_len + rcvd_data_len + 1);
            if (tmp == NULL) {
                syslog(LOG_ERR, "realloc failed, discarding packet");
                free(packet);
                packet = NULL;
                packet_len = 0;
                continue;
            }
            packet = tmp;
            memcpy(packet + packet_len, chunk, rcvd_data_len);
            packet_len += rcvd_data_len;
            packet[packet_len] = '\0';

            char *start = packet;
            char *nl;
            while ((nl = strchr(start, '\n')) != NULL) {
                write(data_fd, start, nl - start + 1);
                start = nl + 1;

                // Send full file contents back after each complete packet.
                // lseek doesn't affect O_APPEND — writes still go to end.
                lseek(data_fd, 0, SEEK_SET);
                char send_buf[RECV_CHUNK];
                ssize_t nr;
                while ((nr = read(data_fd, send_buf, sizeof(send_buf))) > 0)
                    send(connfd, send_buf, nr, 0);
            }

            size_t remaining = packet_len - (start - packet);
            memmove(packet, start, remaining);
            packet_len = remaining;
        }

        free(packet);
        syslog(LOG_DEBUG, "Closed connection from %s", client_ip);
        close(connfd);
    }

    syslog(LOG_DEBUG, "Caught signal, exiting");
    close(server_socket_fd);
    close(data_fd);
    unlink(DATA_FILE);
    closelog();
    return 0;
}

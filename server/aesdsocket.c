#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
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

struct thread_data {
    int connfd;
    int data_fd;
    pthread_mutex_t *mutex;
    char client_ip[INET_ADDRSTRLEN];
};

void *connection_handler_threadfn(void *thread_param) {
    struct thread_data *args = (struct thread_data *)thread_param;

    char *packet = NULL;
    size_t packet_len = 0;
    char chunk[RECV_CHUNK];
    ssize_t rcvd;

    while ((rcvd = recv(args->connfd, chunk, sizeof(chunk), 0)) > 0) {
        char *tmp = realloc(packet, packet_len + rcvd + 1);
        if (!tmp) {
            syslog(LOG_ERR, "realloc failed, discarding packet");
            free(packet);
            packet = NULL;
            packet_len = 0;
            continue;
        }
        packet = tmp;
        memcpy(packet + packet_len, chunk, rcvd);
        packet_len += rcvd;
        packet[packet_len] = '\0';

        char *start = packet;
        char *nl;
        while ((nl = strchr(start, '\n')) != NULL) {
            // Mutex covers write + lseek + read so no two threads interleave file I/O.
            pthread_mutex_lock(args->mutex);
            write(args->data_fd, start, nl - start + 1);
            lseek(args->data_fd, 0, SEEK_SET);
            char send_buf[RECV_CHUNK];
            ssize_t nr;
            while ((nr = read(args->data_fd, send_buf, sizeof(send_buf))) > 0)
                send(args->connfd, send_buf, nr, 0);
            pthread_mutex_unlock(args->mutex);
            start = nl + 1;
        }

        size_t remaining = packet_len - (start - packet);
        memmove(packet, start, remaining);
        packet_len = remaining;
    }

    free(packet);
    syslog(LOG_DEBUG, "Closed connection from %s", args->client_ip);
    close(args->connfd);
    free(args);
    return NULL;
}

static bool start_thread(pthread_mutex_t *mutex, int data_fd, int connfd,
                         const char *client_ip) {
    struct thread_data *td = malloc(sizeof(*td));
    if (!td) return false;
    td->connfd  = connfd;
    td->data_fd = data_fd;
    td->mutex   = mutex;
    strncpy(td->client_ip, client_ip, INET_ADDRSTRLEN - 1);
    td->client_ip[INET_ADDRSTRLEN - 1] = '\0';

    pthread_t thread;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    int rc = pthread_create(&thread, &attr, connection_handler_threadfn, td);
    pthread_attr_destroy(&attr);
    if (rc != 0) {
        free(td);
        return false;
    }
    return true;
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
    if (server_socket_fd == -1) { perror("socket"); return -1; }

    int optval = 1;
    setsockopt(server_socket_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    struct sockaddr_in server_addr = {0};
    server_addr.sin_family      = AF_INET;
    server_addr.sin_port        = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(server_socket_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("bind"); close(server_socket_fd); return -1;
    }

    if (daemon_mode) {
        pid_t pid = fork();
        if (pid < 0) { perror("fork"); close(server_socket_fd); return -1; }
        if (pid > 0) return 0;
        setsid();
    }

    if (listen(server_socket_fd, 5) == -1) {
        perror("listen"); close(server_socket_fd); return -1;
    }

    int data_fd = open(DATA_FILE, O_RDWR | O_CREAT | O_APPEND, 0644);
    if (data_fd == -1) { perror("open"); close(server_socket_fd); return -1; }

    pthread_mutex_t mutex;
    pthread_mutex_init(&mutex, NULL);

    while (!caught_signal) {
        struct sockaddr_in client_addr;
        socklen_t client_addrlen = sizeof(client_addr);
        int connfd = accept(server_socket_fd, (struct sockaddr *)&client_addr, &client_addrlen);
        if (connfd == -1) break;

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        syslog(LOG_DEBUG, "Accepted connection from %s", client_ip);

        if (!start_thread(&mutex, data_fd, connfd, client_ip)) {
            syslog(LOG_ERR, "failed to start thread for %s", client_ip);
            close(connfd);
        }
    }

    syslog(LOG_DEBUG, "Caught signal, exiting");
    close(server_socket_fd);
    close(data_fd);
    pthread_mutex_destroy(&mutex);
    unlink(DATA_FILE);
    closelog();
    return 0;
}

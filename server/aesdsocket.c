#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define SERVER_PORT    9000
#define DATA_FILE      "/var/tmp/aesdsocketdata"
#define RECV_CHUNK     1024
#define TIMER_INTERVAL 10

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

struct timer_data {
    int data_fd;
    pthread_mutex_t *mutex;
};

void *timer_threadfn(void *thread_param) {
    struct timer_data *args = (struct timer_data *)thread_param;

    while (!caught_signal) {
        sleep(TIMER_INTERVAL);
        if (caught_signal) break;

        time_t now = time(NULL);
        struct tm *tm_info = localtime(&now);
        char buf[64];
        // RFC 2822: "timestamp:Fri, 09 Oct 2026 22:00:00 -0400\n"
        strftime(buf, sizeof(buf), "timestamp:%a, %d %b %Y %T %z\n", tm_info);

        pthread_mutex_lock(args->mutex);
        if (write(args->data_fd, buf, strlen(buf)) < 0) {
            syslog(LOG_ERR, "write failed: %s", strerror(errno));
        }
        pthread_mutex_unlock(args->mutex);
    }

    free(args);
    return NULL;
}

void *connection_handler_threadfn(void *thread_param) {
    struct thread_data *args = (struct thread_data *)thread_param;

    char *packet = NULL;
    size_t packet_len = 0;
    char chunk[RECV_CHUNK];
    ssize_t rcvd;

    // while recv reads bytes...
    while ((rcvd = recv(args->connfd, chunk, sizeof(chunk), 0)) > 0) {
        char *tmp = realloc(packet, packet_len + rcvd + 1);
        // Handle realloc running out of mem.
        if (!tmp) {
            syslog(LOG_ERR, "realloc failed, discarding packet");
            free(packet);
            packet = NULL;
            packet_len = 0;
            continue;
        }
        packet = tmp;
        // copy chunk we just recvd into current spot in packet.
        memcpy(packet + packet_len, chunk, rcvd);
        // inc len
        packet_len += rcvd;
        // add delimiter.
        packet[packet_len] = '\0';

        // starting at start of packet, search for a nl.
        char *start = packet;
        char *nl;
        // while we can find nl's in packet..
        while ((nl = strchr(start, '\n')) != NULL) {
            // Mutex covers write + lseek + read so no two threads interleave file I/O.
            pthread_mutex_lock(args->mutex);
            // write up to the nl
            if (write(args->data_fd, start, nl - start + 1) < 0) {
                syslog(LOG_ERR, "write failed: %s", strerror(errno));
            }
            // make sure we're at beginning for the read.
            lseek(args->data_fd, 0, SEEK_SET);
            char send_buf[RECV_CHUNK];
            ssize_t nr;
            // send contents of whole file
            while ((nr = read(args->data_fd, send_buf, sizeof(send_buf))) > 0)
                send(args->connfd, send_buf, nr, 0);
            pthread_mutex_unlock(args->mutex);
            start = nl + 1;
        }

        // rm line from packet
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
    // start detached. auto clean up after returning.
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    int rc = pthread_create(&thread, &attr, connection_handler_threadfn, td);
    // no op
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

    // create and init a mutex
    pthread_mutex_t mutex;
    pthread_mutex_init(&mutex, NULL);

    struct timer_data *td = malloc(sizeof(*td));
    if (!td) { perror("malloc"); close(data_fd); close(server_socket_fd); return -1; }
    td->data_fd = data_fd;
    td->mutex   = &mutex;
    // create timer thread
    pthread_t timer_thread;
    pthread_attr_t timer_attr;
    pthread_attr_init(&timer_attr);
    pthread_attr_setdetachstate(&timer_attr, PTHREAD_CREATE_DETACHED);
    pthread_create(&timer_thread, &timer_attr, timer_threadfn, td);
    pthread_attr_destroy(&timer_attr);

    while (!caught_signal) {
        struct sockaddr_in client_addr;
        socklen_t client_addrlen = sizeof(client_addr);
        // wait for a connection
        int connfd = accept(server_socket_fd, (struct sockaddr *)&client_addr, &client_addrlen);
        if (connfd == -1) break;

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        syslog(LOG_DEBUG, "Accepted connection from %s", client_ip);

        // start a thread for each connection.
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

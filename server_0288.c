#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT 6288
#define NID "7602"
#define MAX_CLIENTS 100
#define MAXLINE 2048
#define MAXNAME 64
#define MAXROOMS 16

typedef struct {
    int fd;
    int active;
    char name[MAXNAME];
    char rooms[MAXROOMS][MAXNAME];
    int room_count;
} Client;

static Client clients[MAX_CLIENTS];
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

static int send_all(int fd, const char *buf, size_t len)
{
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, buf + sent, len - sent, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) return -1;
        sent += (size_t)n;
    }
    return 0;
}

static void reply(int fd, const char *message)
{
    char out[MAXLINE + 128];
    int n = snprintf(out, sizeof(out), "%s NID:%s\n", message, NID);
    if (n > 0 && (size_t)n < sizeof(out))
        send_all(fd, out, (size_t)n);
}

static void send_msg(int fd, const char *message)
{
    char out[MAXLINE + 128];
    int n = snprintf(out, sizeof(out), "%s\n", message);
    if (n > 0 && (size_t)n < sizeof(out))
        send_all(fd, out, (size_t)n);
}

static ssize_t read_line(int fd, char *buf, size_t size)
{
    size_t i = 0;

    while (i + 1 < size) {
        char c;
        ssize_t n = recv(fd, &c, 1, 0);

        if (n == 0) {
            if (i == 0) return 0;
            break;
        }
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (c == '\n') break;
        if (c != '\r') buf[i++] = c;
    }

    buf[i] = '\0';
    return (ssize_t)i;
}

static int find_user(const char *name)
{
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active &&
            clients[i].name[0] != '\0' &&
            strcmp(clients[i].name, name) == 0)
            return i;
    }
    return -1;
}

static int in_room(const Client *c, const char *room)
{
    for (int i = 0; i < c->room_count; i++) {
        if (strcmp(c->rooms[i], room) == 0)
            return 1;
    }
    return 0;
}

static void *handle_client(void *arg)
{
    int slot = *(int *)arg;
    free(arg);

    int fd = clients[slot].fd;
    char buffer[MAXLINE];

    while (1) {
        ssize_t n = read_line(fd, buffer, sizeof(buffer));
        if (n <= 0) break;
        if (buffer[0] == '\0') continue;

        char command[32] = "";
        char arg1[MAXNAME] = "";
        int fields = sscanf(buffer, "%31s %63s", command, arg1);

        if (strcmp(command, "REGISTER") == 0) {
            char username[MAXNAME] = "";

            if (sscanf(buffer, "REGISTER %63s", username) != 1) {
                reply(fd, "ERR 005 INVALID_USERNAME");
                continue;
            }

            pthread_mutex_lock(&mutex);
            int taken = find_user(username);
            if (taken < 0) {
                snprintf(clients[slot].name,
                         sizeof(clients[slot].name), "%s", username);
            }
            pthread_mutex_unlock(&mutex);

            if (taken >= 0) {
                reply(fd, "ERR 001 USERNAME_TAKEN");
            } else {
                char response[MAXLINE];
                snprintf(response, sizeof(response),
                         "OK REGISTERED %s", username);
                reply(fd, response);
            }
            continue;
        }

        pthread_mutex_lock(&mutex);
        int registered = clients[slot].name[0] != '\0';
        char sender[MAXNAME];
        snprintf(sender, sizeof(sender), "%s", clients[slot].name);
        pthread_mutex_unlock(&mutex);

        if (!registered) {
            reply(fd, "ERR 006 REGISTER_FIRST");
            continue;
        }

        if (strcmp(command, "LIST") == 0) {
            char users[MAXLINE] = "OK USERS ";
            int count = 0;

            pthread_mutex_lock(&mutex);
            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (clients[i].active && clients[i].name[0] != '\0') {
                    if (count++)
                        strncat(users, ",",
                                sizeof(users) - strlen(users) - 1);
                    strncat(users, clients[i].name,
                            sizeof(users) - strlen(users) - 1);
                }
            }
            pthread_mutex_unlock(&mutex);
            reply(fd, users);
        }
        else if (strcmp(command, "BCAST") == 0) {
            const char *p = strchr(buffer, ' ');
            if (!p || !p[1]) {
                reply(fd, "ERR 005 INVALID_MESSAGE");
                continue;
            }

            char out[MAXLINE];
            snprintf(out, sizeof(out), "MSG BCAST %s %s", sender, p + 1);

            pthread_mutex_lock(&mutex);
            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (clients[i].active && i != slot &&
                    clients[i].name[0] != '\0')
                    send_msg(clients[i].fd, out);
            }
            pthread_mutex_unlock(&mutex);
            reply(fd, "OK SENT");
        }
        else if (strcmp(command, "PMSG") == 0) {
            char target[MAXNAME] = "";
            int used = 0;

            if (sscanf(buffer, "PMSG %63s %n", target, &used) != 1 ||
                used <= 0 || buffer[used] == '\0') {
                reply(fd, "ERR 005 INVALID_MESSAGE");
                continue;
            }

            char out[MAXLINE];
            snprintf(out, sizeof(out), "MSG PRIV %s %s",
                     sender, buffer + used);

            pthread_mutex_lock(&mutex);
            int idx = find_user(target);
            if (idx >= 0) send_msg(clients[idx].fd, out);
            pthread_mutex_unlock(&mutex);

            if (idx >= 0) reply(fd, "OK SENT");
            else reply(fd, "ERR 002 USER_NOT_FOUND");
        }
        else if (strcmp(command, "JOIN") == 0) {
            if (fields < 2) {
                reply(fd, "ERR 005 INVALID_ROOM");
                continue;
            }

            pthread_mutex_lock(&mutex);
            if (in_room(&clients[slot], arg1)) {
                pthread_mutex_unlock(&mutex);
                reply(fd, "ERR 004 ALREADY_IN_ROOM");
                continue;
            }
            if (clients[slot].room_count >= MAXROOMS) {
                pthread_mutex_unlock(&mutex);
                reply(fd, "ERR 004 ROOM_LIMIT");
                continue;
            }

            snprintf(clients[slot].rooms[clients[slot].room_count],
                     MAXNAME, "%s", arg1);
            clients[slot].room_count++;
            pthread_mutex_unlock(&mutex);

            char out[MAXLINE];
            snprintf(out, sizeof(out), "OK JOINED %s", arg1);
            reply(fd, out);
        }
        else if (strcmp(command, "LEAVE") == 0) {
            if (fields < 2) {
                reply(fd, "ERR 005 INVALID_ROOM");
                continue;
            }

            int found = 0;
            pthread_mutex_lock(&mutex);

            for (int i = 0; i < clients[slot].room_count; i++) {
                if (strcmp(clients[slot].rooms[i], arg1) == 0) {
                    for (int j = i; j < clients[slot].room_count - 1; j++) {
                        memmove(clients[slot].rooms[j],
                                clients[slot].rooms[j + 1], MAXNAME);
                    }
                    clients[slot].room_count--;
                    found = 1;
                    break;
                }
            }
            pthread_mutex_unlock(&mutex);

            if (found) {
                char out[MAXLINE];
                snprintf(out, sizeof(out), "OK LEFT %s", arg1);
                reply(fd, out);
            } else {
                reply(fd, "ERR 003 ROOM_NOT_FOUND");
            }
        }
        else if (strcmp(command, "ROOMS") == 0) {
            char rooms[MAXLINE] = "OK ROOMS ";
            int count = 0;

            pthread_mutex_lock(&mutex);
            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (!clients[i].active) continue;
                for (int j = 0; j < clients[i].room_count; j++) {
                    const char *room = clients[i].rooms[j];
                    int seen = 0;

                    for (int k = 0; k < i; k++) {
                        if (clients[k].active && in_room(&clients[k], room)) {
                            seen = 1;
                            break;
                        }
                    }
                    for (int k = 0; k < j && !seen; k++) {
                        if (strcmp(clients[i].rooms[k], room) == 0)
                            seen = 1;
                    }

                    if (!seen) {
                        if (count++)
                            strncat(rooms, ",",
                                    sizeof(rooms) - strlen(rooms) - 1);
                        strncat(rooms, room,
                                sizeof(rooms) - strlen(rooms) - 1);
                    }
                }
            }
            pthread_mutex_unlock(&mutex);
            reply(fd, rooms);
        }
        else if (strcmp(command, "RMSG") == 0) {
            char room[MAXNAME] = "";
            int used = 0;

            if (sscanf(buffer, "RMSG %63s %n", room, &used) != 1 ||
                used <= 0 || buffer[used] == '\0') {
                reply(fd, "ERR 005 INVALID_MESSAGE");
                continue;
            }

            char out[MAXLINE];
            snprintf(out, sizeof(out), "MSG ROOM %s %s %s",
                     room, sender, buffer + used);

            pthread_mutex_lock(&mutex);
            int member = in_room(&clients[slot], room);
            if (member) {
                for (int i = 0; i < MAX_CLIENTS; i++) {
                    if (clients[i].active && i != slot &&
                        in_room(&clients[i], room))
                        send_msg(clients[i].fd, out);
                }
            }
            pthread_mutex_unlock(&mutex);

            if (member) reply(fd, "OK SENT");
            else reply(fd, "ERR 003 ROOM_NOT_FOUND");
        }
        else if (strcmp(command, "SENDFILE") == 0) {
            reply(fd, "ERR 007 FILE_TRANSFER_NOT_SUPPORTED");
        }
        else if (strcmp(command, "QUIT") == 0) {
            reply(fd, "OK BYE");
            break;
        }
        else {
            reply(fd, "ERR 005 UNKNOWN_COMMAND");
        }
    }

    pthread_mutex_lock(&mutex);
    clients[slot].active = 0;
    clients[slot].fd = -1;
    clients[slot].name[0] = '\0';
    clients[slot].room_count = 0;
    pthread_mutex_unlock(&mutex);

    close(fd);
    return NULL;
}

int main(void)
{
    signal(SIGPIPE, SIG_IGN);

    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0) {
        perror("socket");
        return 1;
    }

    int opt = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(PORT);

    if (bind(listenfd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind");
        close(listenfd);
        return 1;
    }

    if (listen(listenfd, MAX_CLIENTS) < 0) {
        perror("listen");
        close(listenfd);
        return 1;
    }

    printf("NetMessenger server started on port %d\n", PORT);
    printf("NID:%s\n", NID);
    fflush(stdout);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        clients[i].fd = -1;
        clients[i].active = 0;
        clients[i].name[0] = '\0';
        clients[i].room_count = 0;
    }

    while (1) {
        struct sockaddr_in client_address;
        socklen_t addrlen = sizeof(client_address);

        int fd = accept(listenfd, (struct sockaddr *)&client_address,
                        &addrlen);
        if (fd < 0) {
            if (errno == EINTR) continue;
            perror("accept");
            continue;
        }

        pthread_mutex_lock(&mutex);
        int slot = -1;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (!clients[i].active) {
                slot = i;
                clients[i].active = 1;
                clients[i].fd = fd;
                clients[i].name[0] = '\0';
                clients[i].room_count = 0;
                break;
            }
        }
        pthread_mutex_unlock(&mutex);

        if (slot < 0) {
            reply(fd, "ERR 008 SERVER_FULL");
            close(fd);
            continue;
        }

        int *arg = malloc(sizeof(int));
        if (!arg) {
            pthread_mutex_lock(&mutex);
            clients[slot].active = 0;
            clients[slot].fd = -1;
            pthread_mutex_unlock(&mutex);
            close(fd);
            continue;
        }

        *arg = slot;
        pthread_t tid;

        if (pthread_create(&tid, NULL, handle_client, arg) != 0) {
            free(arg);
            pthread_mutex_lock(&mutex);
            clients[slot].active = 0;
            clients[slot].fd = -1;
            pthread_mutex_unlock(&mutex);
            close(fd);
            continue;
        }

        pthread_detach(tid);
    }

    close(listenfd);
    return 0;
}

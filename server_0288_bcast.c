#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define PORT 6288
#define BACKLOG 10
#define MAXLINE 1024

void *handle_client(void *arg)
{
    int connfd = *(int *)arg;
    char buffer[MAXLINE];

    free(arg);

    printf("Client thread started.\n");

    while (1)
    {
        ssize_t n = recv(connfd, buffer, sizeof(buffer) - 1, 0);

        if (n < 0)
        {
            perror("recv");
            break;
        }

        if (n == 0)
        {
            printf("Client disconnected.\n");
            break;
        }

        buffer[n] = '\0';

        printf("Received: %s", buffer);

        if (strncmp(buffer, "REGISTER ", 9) == 0)
        {
            char username[100];
            char response[MAXLINE];

            if (sscanf(buffer + 9, "%99s", username) == 1)
            {
                snprintf(response,
                         sizeof(response),
                         "OK REGISTERED NID:7602\n");

                if (send(connfd,
                         response,
                         strlen(response),
                         0) < 0)
                {
                    perror("send");
                    break;
                }

                printf("Response sent: %s", response);
                printf("User registered: %s\n", username);
            }
        }
else if (strncmp(buffer, "LIST", 4) == 0)
{
    char response[MAXLINE];

    snprintf(response,
             sizeof(response),
             "OK LIST Chalitha-client NID:7602\n");

    send(connfd, response, strlen(response), 0);

    printf("LIST response sent.\n");
}

else if (strncmp(buffer, "BCAST ", 6) == 0)
{
    char response[MAXLINE];

    snprintf(response,
             sizeof(response),
             "OK BCAST RECEIVED NID:7602\n");

    send(connfd,
         response,
         strlen(response),
         0);

    printf("BCAST message received: %s", buffer + 6);
}



       else if (strncmp(buffer, "QUIT", 4) == 0)
        {
            char response[MAXLINE];

            snprintf(response,
                     sizeof(response),
                     "OK BYE NID:7602\n");

            send(connfd, response, strlen(response), 0);

            printf("Client requested QUIT.\n");

            break;
        }
        else
        {
            char response[MAXLINE];

            snprintf(response,
                     sizeof(response),
                     "ERR 999 UNKNOWN_COMMAND NID:7602\n");

            send(connfd, response, strlen(response), 0);

            printf("Unknown command.\n");
        }
    }

    close(connfd);

    printf("Client thread finished.\n");

    return NULL;
}

int main(void)
{
    int listenfd;
    struct sockaddr_in server_addr;

    listenfd = socket(AF_INET, SOCK_STREAM, 0);

    if (listenfd < 0)
    {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    int opt = 1;

    if (setsockopt(listenfd,
                   SOL_SOCKET,
                   SO_REUSEADDR,
                   &opt,
                   sizeof(opt)) < 0)
    {
        perror("setsockopt");
        close(listenfd);
        exit(EXIT_FAILURE);
    }

    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(PORT);

    if (bind(listenfd,
             (struct sockaddr *)&server_addr,
             sizeof(server_addr)) < 0)
    {
        perror("bind");
        close(listenfd);
        exit(EXIT_FAILURE);
    }

    if (listen(listenfd, BACKLOG) < 0)
    {
        perror("listen");
        close(listenfd);
        exit(EXIT_FAILURE);
    }

    printf("NetMessenger server started.\n");
    printf("Listening on TCP port %d\n", PORT);
    printf("NID:7602\n");

    while (1)
    {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int connfd = accept(listenfd,
                            (struct sockaddr *)&client_addr,
                            &client_len);

        if (connfd < 0)
        {
            perror("accept");
            continue;
        }

        printf("Client connected: %s\n",
               inet_ntoa(client_addr.sin_addr));

        int *client_socket = malloc(sizeof(int));

        if (client_socket == NULL)
        {
            perror("malloc");
            close(connfd);
            continue;
        }

        *client_socket = connfd;

        pthread_t thread_id;

        if (pthread_create(&thread_id,
                           NULL,
                           handle_client,
                           client_socket) != 0)
        {
            perror("pthread_create");
            close(connfd);
            free(client_socket);
            continue;
        }

        pthread_detach(thread_id);
    }

    close(listenfd);

    return 0;
}


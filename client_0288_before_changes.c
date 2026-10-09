#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>

#define SERVER_IP "127.0.0.1"
#define PORT 6288
#define MAXLINE 1024

int main(void)
{
    int sockfd;
    struct sockaddr_in server_addr;
    char sendline[MAXLINE];
    char recvline[MAXLINE];

    sockfd = socket(AF_INET, SOCK_STREAM, 0);

    if (sockfd < 0)
    {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);

    if (inet_pton(AF_INET,
                  SERVER_IP,
                  &server_addr.sin_addr) <= 0)
    {
        perror("inet_pton");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    if (connect(sockfd,
                (struct sockaddr *)&server_addr,
                sizeof(server_addr)) < 0)
    {
        perror("connect");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    printf("Connected to NetMessenger server.\n");

    while (1)
    {
        fd_set readfds;

        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        FD_SET(sockfd, &readfds);

        int maxfd = sockfd;

        if (select(maxfd + 1,
                   &readfds,
                   NULL,
                   NULL,
                   NULL) < 0)
        {
            perror("select");
            break;
        }

        /* Message received from server */
        if (FD_ISSET(sockfd, &readfds))
        {
            ssize_t n = recv(sockfd,
                             recvline,
                             sizeof(recvline) - 1,
                             0);

            if (n < 0)
            {
                perror("recv");
                break;
            }

            if (n == 0)
            {
                printf("\nServer closed the connection.\n");
                break;
            }

            recvline[n] = '\0';

            printf("\nServer: %s", recvline);
            printf("NetMessenger> ");
            fflush(stdout);
        }

        /* User typed a command */
        if (FD_ISSET(STDIN_FILENO, &readfds))
        {

            if (fgets(sendline,
                      sizeof(sendline),
                      stdin) == NULL)
            {
                break;
            }

            if (send(sockfd,
                     sendline,
                     strlen(sendline),
                     0) < 0)
            {
                perror("send");
                break;
            }

            if (strncmp(sendline, "QUIT", 4) == 0)
            {
                break;
            }
        }
    }

    close(sockfd);

    return 0;
}

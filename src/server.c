#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>

#define QUEUE_SIZE 4
#define BUFFER_SIZE 1024
#define PORT 4444
#define MAX_CLIENTS 10
#define MAX_GAMES 10


int clients[MAX_CLIENTS];
struct sockaddr_in clients_addrs[MAX_CLIENTS];
enum client_state {
    Menu, LookingToPlay, Playing, Spectating, Closed
};
enum client_state clients_state[MAX_CLIENTS];
int server_fd;
int server_port;
enum game_state {
    OnGoing, Finished
};
int games[MAX_GAMES];
enum game_state games_state[MAX_GAMES];
int ongoing_games;
int fd;

//communication protocol, we send msg size following \n, then the msg
void send_str(int socket, struct sockaddr *address, char *str) {
    char buffer[1024];
    sprintf(buffer, "%d\n%s", strlen(str), str);
    sendto(socket, buffer, strlen(buffer), 0, address, sizeof(*address));
}

void send_char(int socket, struct sockaddr *address, char ch) {
    char buffer[1024];
    sprintf(buffer, "1\n%c", ch);
    sendto(socket, buffer, strlen(buffer), 0, address, sizeof(*address));
}

void send_int(int socket, struct sockaddr *address, int num) {
    char buffer[1024];
    sprintf(buffer, "%d", num);
    sprintf(buffer, "%d\n%d", strlen(buffer), num);
    sendto(socket, buffer, strlen(buffer), 0, address, sizeof(*address));
}

//reading size of msg
int size(int socket, struct sockaddr *address, socklen_t *len) {
    char buffer[1024];
    int i = 0;
    int bytes = recvfrom(socket, &buffer[i], 1, 0, address, len);
    while (buffer[i] != '\n') {
        recvfrom(socket, &buffer[++i], 1, 0, address, len);
    }
    buffer[i] = '\0';
    return atoi(buffer);
}

int read_int(int socket, struct sockaddr *address, socklen_t *len) {
    char buffer[1024];
    int n = size(socket, address, len);
    recvfrom(socket, buffer, n, 0, address, len);
    buffer[n] = '\0';
    return atoi(buffer);
}

void read_str(int socket, struct sockaddr *address, socklen_t *len, char *buffer) {
    int n = size(socket, address, len);
    recvfrom(socket, buffer, n, 0, address, len);
    buffer[n] = '\0';
}

char read_char(int socket, struct sockaddr *address, socklen_t *len) {
    char buffer[1024];
    int n = size(socket, address, len);
    recvfrom(socket, buffer, n, 0, address, len);
    buffer[n] = '\0';
    return buffer[0];
}

int init_server(int port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == 0) {
        printf("socket creation failed\n");
        return 0;
    }
    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = inet_addr("127.0.0.1");
    address.sin_port = htons(port);
    if (bind(sock, (const struct sockaddr *) &address, sizeof(address)) < 0) {
        printf("binding failed\n");
        return 0;
    }
    if (listen(sock, QUEUE_SIZE) < 0) {
        printf("listening failed\n");
        return 0;
    };
    return sock;
}

void init_client(int client_fd, struct sockaddr_in *address) {
    char buffer[BUFFER_SIZE];
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i] == 0) {
            clients[i] = client_fd;
            clients_addrs[i] = *address;
            sprintf(buffer, "client %d (%s, %d) connected to server\n", i, inet_ntoa(address->sin_addr),
                    ntohs(address->sin_port));
            write(STDOUT_FILENO, buffer, strlen(buffer));
            clients_state[i] = Menu;
            send_str(client_fd, (struct sockaddr *) address, "connected");
            return;
        }
    }
}

void create_match(int client1, int client2) {
    int sock, broadcast = 1, opt = 1;
    char buffer[1024];
    struct sockaddr_in bc_address;
    socklen_t size;

    int game = 0;
    while (games_state[game] != Finished) {
        game++;
        if (game == MAX_GAMES) {
            printf("not enough space to create match!\n");
            break;
        }
    }
    int port = game == 0 ? server_port : games[game - 1];
    while (1) {
        sock = socket(AF_INET, SOCK_DGRAM, 0);
        setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));
        setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
        bc_address.sin_family = AF_INET;
        bc_address.sin_port = ++port;
        bc_address.sin_addr.s_addr = inet_addr("255.255.255.255");
        if (bind(sock, (struct sockaddr *) &bc_address, sizeof(bc_address)) < 0) {
            close(sock);
            continue;
        }
        close(sock);
        games[game] = port;
        games_state[game] = OnGoing;
        send_str(clients[client1], (struct sockaddr *) &clients_addrs[client1], "port");
        send_int(clients[client1], (struct sockaddr *) &clients_addrs[client1], port);
        send_char(clients[client1], (struct sockaddr *) &clients_addrs[client1], 'X');
        send_str(clients[client2], (struct sockaddr *) &clients_addrs[client2], "port");
        send_int(clients[client2], (struct sockaddr *) &clients_addrs[client2], port);
        send_char(clients[client2], (struct sockaddr *) &clients_addrs[client2], 'O');
        clients_state[client1] = Playing;
        clients_state[client2] = Playing;
        ongoing_games++;
        break;
    }
    sprintf(buffer, "new game created on port %d, players: client %d, client %d\n", port, client1, client2);
    write(STDOUT_FILENO, buffer, strlen(buffer));
}

void end_match(int port, char result) {
    int idx = -1;
    for (int i = 0; i < MAX_GAMES; ++i) {
        if (games[i] == port) {
            idx = i;
            break;
        }
    }
    if (idx == -1)
        return;
    if (result != '=') {
        printf("game %d finished! %c won\n", idx + 1, result);
    } else {
        printf("game %d finished! draw\n", idx + 1);
    }
    char buffer[BUFFER_SIZE];
    sprintf(buffer, "game on port %d finished, result: %c\n", port, result);
    write(fd, buffer, strlen(buffer));
    games[idx] = 0;
    games_state[idx] = Finished;
    ongoing_games--;
}

void handle_client(int client) {
    char buffer[BUFFER_SIZE];
    read_str(clients[client], (struct sockaddr *) &clients_addrs[client], NULL, buffer);
    if (strlen(buffer) == 0 || strcmp(buffer, "close") == 0) {
        close(clients[client]);
        clients[client] = 0;
        clients_state[client] = Closed;
        sprintf(buffer, "client %d closed connection\n", client);
        write(STDOUT_FILENO, buffer, strlen(buffer));
        return;
    }

    switch (clients_state[client]) {
        case Menu:
            if (strcmp(buffer, "play") == 0) {
                for (int i = 0; i < MAX_CLIENTS; ++i) {
                    if (clients_state[i] == LookingToPlay && i != client) {
                        create_match(client, i);
                        return;
                    }
                }
                send_str(clients[client], (struct sockaddr *) &clients_addrs[client], "waiting");
                clients_state[client] = LookingToPlay;
            } else if (strcmp(buffer, "spectate") == 0) {
                send_str(clients[client], (struct sockaddr *) &clients_addrs[client], "games");
                send_int(clients[client], (struct sockaddr *) &clients_addrs[client], ongoing_games);
                for (int i = 0; i < MAX_GAMES; ++i) {
                    if (games_state[i] == OnGoing) {
                        send_int(clients[client], (struct sockaddr *) &clients_addrs[client], games[i]);
                    }
                }
                clients_state[client] = Spectating;
                if (ongoing_games == 0)
                    clients_state[client] = Menu;
            } else {
                send_str(clients[client], (struct sockaddr *) &clients_addrs[client], "invalid");
            }
            break;
        case Playing:
            if (strcmp(buffer, "finished") == 0) {
                int port = read_int(clients[client], (struct sockaddr *) &clients_addrs[client], NULL);
                char result = read_char(clients[client], (struct sockaddr *) &clients_addrs[client], NULL);
                end_match(port, result);
            }
            break;
        default:
            printf("client %d sent unhandled msg \"%s\"", client, buffer);
            break;
    }
}

void close_server() {
    close(fd);
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (clients_state[i] != Closed) {
            send_str(clients[i], (struct sockaddr *) &clients_addrs[i], "close");
            close(clients_state[i]);
        }
    }
    if (server_fd)
        close(server_fd);
    char msg[] = "server closed\n";
    write(STDOUT_FILENO, msg, strlen(msg));
    exit(EXIT_SUCCESS);
}

int main(int argc, char *argv[]) {
    server_port = PORT;
    server_fd = 0;
    if (argc > 1) {
        server_port = atoi(argv[1]);
    }
    server_fd = init_server(server_port);
    if (server_fd == 0) {
        printf("server creation failed, exiting...\n");
        exit(EXIT_FAILURE);
    }
    printf("server is running...\n");
    ongoing_games = 0;
    fd = open("results.txt", O_CREAT | O_RDWR, 0777);
    lseek(fd, 0, SEEK_END);
    struct sockaddr_in address;
    socklen_t address_size;
    fd_set readfds;
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        clients[i] = 0;
        clients_state[i] = Closed;
    }
    for (int i = 0; i < MAX_GAMES; ++i) {
        games[i] = 0;
        games_state[i] = Finished;
    }
    char buffer[BUFFER_SIZE];
    while (1) {
        signal(SIGKILL, close_server);
        signal(SIGINT, close_server);
        FD_ZERO(&readfds);
        FD_SET(server_fd, &readfds);
        int max_sd = server_fd;
        for (int i = 0; i < MAX_CLIENTS; ++i) {
            if (clients[i])
                FD_SET(clients[i], &readfds);
            if (clients[i] > max_sd)
                max_sd = clients[i];
        }
        int activity = select(max_sd + 1, &readfds, NULL, NULL, NULL);
        if (activity < 0) {
            exit(EXIT_FAILURE);
        }
        if (FD_ISSET(server_fd, &readfds)) {
            int client_fd = accept(server_fd, (struct sockaddr *) &address, &address_size);
            init_client(client_fd, &address);
        }
        for (int i = 0; i < MAX_CLIENTS; ++i) {
            if (FD_ISSET(clients[i], &readfds)) {
                handle_client(i);
            }
        }
    }
}
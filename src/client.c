#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>

#define BUFFER_SIZE 1024
#define PORT 4444
#define TIME 5

int client_fd = 0;
int game_port;
int game_fd = 0;
int has_time = 1;
struct sockaddr_in game_addr, server_addr;
char ch;
enum client_state {
    Menu, LookingToPlay, Playing, Spectating, Closed
};
enum client_state state;
enum game_state {
    OnGoing, Finished
};
struct Game {
    char table[9];
    char turn;
} game;

enum type {
    LOG, ERROR, USER
};

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
    recvfrom(socket, &buffer[i], 1, 0, address, len);
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

void print(char *log, enum type type) {
    switch (type) {
        case LOG:
            write(STDOUT_FILENO, "LOG\t", strlen("LOG\t"));
            write(STDOUT_FILENO, log, strlen(log));
            break;
        case ERROR:
            write(STDOUT_FILENO, "ERROR\t", strlen("ERROR\t"));
            write(STDOUT_FILENO, log, strlen(log));
            break;
        case USER:
            write(STDOUT_FILENO, log, strlen(log));
            break;
        default:
            write(STDOUT_FILENO, log, strlen(log));
            break;
    }

}

int connect_server(int port) {
    int fd;
    fd = socket(AF_INET, SOCK_STREAM, 0);
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    if (connect(fd, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
        print("connecting to server failed\n", ERROR);
        return 0;
    }
    return fd;
}

void close_client() {
     send_str(client_fd, (struct sockaddr *) &server_addr, "close");
    if (client_fd)
        close(client_fd);
    if (game_fd)
        close(game_fd);
    print("client closed\n", LOG);
    exit(EXIT_SUCCESS);
}

void connect_game() {
    int broadcast = 1, opt = 1;
    game_fd = socket(AF_INET, SOCK_DGRAM, 0);
    setsockopt(game_fd, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));
    setsockopt(game_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));

    game_addr.sin_family = AF_INET;
    game_addr.sin_port = htons(game_port);
    game_addr.sin_addr.s_addr = inet_addr("255.255.255.255");

    if (bind(game_fd, (const struct sockaddr *) &game_addr, sizeof(game_addr)) < 0) {
        print("cannot connect to game\n", ERROR);
        close_client();
        return;
    }
    print("connected to game\n", LOG);
}

void broadcast_game(char turn) {
    char buffer[BUFFER_SIZE];
    for (int i = 0; i < 9; i++) {
        buffer[i] = game.table[i];
    }
    buffer[9] = turn;
    buffer[10] = '\0';
    sendto(game_fd, buffer, strlen(buffer), 0, (struct sockaddr *) &game_addr, sizeof(game_addr));
}

void init_game(int broadcast) {
    for (int i = 0; i < 9; ++i) {
        game.table[i] = ' ';
    }
    game.turn = 'O';
    if (broadcast) {
        broadcast_game(ch);
    }
}

void close_game() {
    close(game_fd);
    game_fd = 0;
}

int make_move(int move) {
    if (move > 9 || move < 1) {
        return 0;
    }
    if (game.table[move - 1] != ' ')
        return 0;
    game.table[move - 1] = ch;
    game.turn = game.turn == 'X' ? 'O' : 'X';
    return 1;
}

int game_finished() {
    for (int i = 0; i < 3; ++i) {
        if (game.table[i * 3] != ' ' && game.table[i * 3] == game.table[i * 3 + 1] &&
            game.table[i * 3 + 1] == game.table[i * 3 + 2])
            return game.turn;
        if (game.table[i] != ' ' && game.table[i] == game.table[i + 3] && game.table[i + 3] == game.table[i + 6])
            return game.turn;
    }
    if (game.table[0] != ' ' && game.table[0] == game.table[4] && game.table[4] == game.table[8])
        return game.turn;
    if (game.table[2] != ' ' && game.table[2] == game.table[4] && game.table[4] == game.table[6])
        return game.turn;
    for (int i = 0; i < 8; ++i) {
        if (game.table[i] == ' ')
            return 0;
    }
    return -1;
}

void exit_game(char result) {
    if (state == Playing) {
        send_str(client_fd, (struct sockaddr *) &server_addr, "finished");
        send_int(client_fd, (struct sockaddr *) &server_addr, game_port);
        send_char(client_fd, (struct sockaddr *) &server_addr, result);
    }
    close_client();
}

void handle_game() {
    char buffer[BUFFER_SIZE];
    socklen_t len;
    recvfrom(game_fd, buffer, sizeof(buffer), 0, (struct sockaddr *) &game_addr, &len);
//    read_str(game_fd, (struct sockaddr *) &game_addr, &len, buffer);
    if (strlen(buffer) == 0) {
        print("connection to game closed\n", LOG);
        close_game();
    }
    close(game_fd);
    for (int i = 0; i < 9; i++) {
        game.table[i] = buffer[i];
    }
    game.turn = buffer[9];
    sprintf(buffer, "%c|%c|%c|\n%c|%c|%c|\n%c|%c|%c|\n", game.table[0], game.table[1],
            game.table[2], game.table[3], game.table[4],
            game.table[5], game.table[6], game.table[7], game.table[8]);
    print(buffer, USER);
    int result = game_finished();
    switch (result) {
        case 'X':
            print("O won the game\n", USER);
            exit_game('O');
            break;
        case 'O':
            print("X won the game\n", USER);
            exit_game('X');
            break;
        case -1:
            print("draw\n", USER);
            exit_game('=');
            break;
        default:
            sprintf(buffer, "turn: %c\n", game.turn);
            print(buffer, USER);
    }
    connect_game();
    has_time = 1;
    alarm(TIME);

}

void handle_server(int sockfd) {
    char buffer[BUFFER_SIZE];
    read_str(sockfd, (struct sockaddr *) &server_addr, NULL, buffer);
    if (strlen(buffer) == 0 || strcmp(buffer, "close") == 0) {
        print("connection to server closed\n", LOG);
        close_client();
    }
    switch (state) {
        case Closed:
            if (strcmp(buffer, "connected") == 0) {
                print("welcome\nsend \"play\" to play a new game or \"spectate\" to spectate a game\n", USER);
                state = Menu;
            }
            break;
        case Menu:
            if (strcmp(buffer, "port") == 0) {
                game_port = read_int(sockfd, (struct sockaddr *) &server_addr, NULL);
                ch = read_char(sockfd, (struct sockaddr *) &server_addr, NULL);
                sprintf(buffer, "game is running on port %d, you are playing as %c\n", game_port, ch);
                print(buffer, USER);
                state = Playing;
                connect_game();
                init_game(ch == 'O' ? 1 : 0);
            } else if (strcmp(buffer, "waiting") == 0) {
                print("looking for opponent...\n", USER);
                state = LookingToPlay;
            } else if (strcmp(buffer, "invalid") == 0) {
                print("invalid command\nsend \"play\" to play a new game or \"spectate\" to spectate a game\n", USER);
                state = Menu;
            } else if (strcmp(buffer, "games") == 0) {
                int games = read_int(sockfd, (struct sockaddr *) &server_addr, NULL);
                if (games == 0) {
                    print("no games found to spectate\nsend \"play\" to play a new game or \"spectate\" to spectate a game\n",
                          USER);
                    state = Menu;
                } else {
                    print("choose a port:\n", USER);
                    for (int i = 0; i < games; ++i) {
                        int port = read_int(sockfd, (struct sockaddr *) &server_addr, NULL);
                        sprintf(buffer, "%d. %d\n", i + 1, port);
                        print(buffer, USER);
                    }
                    state = Spectating;
                }

            }
            break;
        case LookingToPlay:
            if (strcmp(buffer, "port") == 0) {
                game_port = read_int(sockfd, (struct sockaddr *) &server_addr, NULL);
                ch = read_char(sockfd, (struct sockaddr *) &server_addr, NULL);
                sprintf(buffer, "game is running on port %d, you are playing as %c\n", game_port, ch);
                print(buffer, USER);
                state = Playing;
                connect_game();
                init_game(ch == 'O' ? 1 : 0);
            }
            break;
    }
}

void time_passed() {
    if (state == Playing) {
        print("your time finished\n", USER);
        broadcast_game(game.turn == 'X' ? 'O' : 'X');
        has_time = 0;
    }

}


int main(int argc, char *argv[]) {
    int port = PORT;
    if (argc > 1) {
        port = atoi(argv[1]);
    }
    state = Closed;
    client_fd = connect_server(port);
    if (client_fd == 0) {
        exit(EXIT_FAILURE);
    }
    print("client connected to server\n", LOG);
    fd_set readfds;
    char buffer[BUFFER_SIZE];
    signal(SIGKILL, close_client);
    signal(SIGINT, close_client);
    signal(SIGALRM, time_passed);
    siginterrupt(SIGALRM, 1);
    while (1) {
        FD_ZERO(&readfds);
        FD_SET(client_fd, &readfds);
        FD_SET(STDIN_FILENO, &readfds);
        int max_sd = client_fd > STDIN_FILENO ? client_fd : STDIN_FILENO;
        if (game_fd) {
            FD_SET(game_fd, &readfds);
            max_sd = game_fd > max_sd ? game_fd : max_sd;
        }
        int activity = select(max_sd + 1, &readfds, NULL, NULL, NULL);
        if (activity < 0) {
            if(!has_time)
                continue;
            exit(EXIT_FAILURE);
        }
        if (game_fd && FD_ISSET(game_fd, &readfds)) {
            handle_game();
        }
        if (FD_ISSET(client_fd, &readfds)) {
            handle_server(client_fd);
        }
        if (FD_ISSET(STDIN_FILENO, &readfds)) {
            switch (state) {
                case Playing: {
                    read(STDIN_FILENO, buffer, BUFFER_SIZE);
                    if (game.turn != ch) {
                        print("not your turn\n", USER);
                    } else if (has_time) {
                        char *ptr;
                        if (make_move(strtol(buffer, &ptr, 10))) {
                            broadcast_game(ch == 'X' ? 'O' : 'X');
                        } else {
                            print("invalid move! try again\n", USER);
                        }
                    }
                    break;
                }

                case Spectating: {
                    ssize_t bytes = read(STDIN_FILENO, buffer, BUFFER_SIZE);
                    buffer[bytes - 1] = '\0';
                    game_port = atoi(buffer);
                    connect_game();
                    break;
                }

                default: {
                    ssize_t bytes = read(STDIN_FILENO, buffer, BUFFER_SIZE);
                    buffer[bytes - 1] = '\0';
                    send_str(client_fd, (struct sockaddr *) &server_addr, buffer);
                }
            }
        }

    }
}


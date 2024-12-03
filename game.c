#include "../include/game.h"
#include <ncurses.h>
#include <stdio.h>
#include <stdnoreturn.h>
#define SELECT_TIMEOUT_USEC 100000
#define BUFFER_SIZE 1024
#define GAME_GRID_SIZE 20
#define TEN 10

typedef struct
{
    int                     socket;
    int                     hostx;
    int                     hosty;
    int                     clientx;
    int                     clienty;
    bool                    is_host;
    struct sockaddr_storage peer_addr;
    socklen_t               peer_addr_len;
    char                    game_state[BUFFER_SIZE];
} Context;

static Context               context;          // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
static volatile sig_atomic_t quit_flag = 0;    // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

// Signal handler for graceful termination on SIGINT (Ctrl+C)
void handle_signal(int signal)
{
    (void)signal;
    quit_flag = 1;
}

// Sets up the connection between the host and client
// Initializes the address structure and binds to the port if hosting or sets up connection if connecting
int setupConnection(const int *sockfd, struct sockaddr_storage *addr, const char *ip_address, const char *port)
{
    memset(addr, 0, sizeof(*addr));    // Zero out address structure

    if(ip_address == NULL)
    {
        in_port_t parsed_port;
        // Hosting mode
        printf("No IP address provided. Hosting the game...\n");
        context.is_host = true;

        // Bind to the port
        parsed_port     = parse_in_port_t("game", port);
        addr->ss_family = AF_INET;
        socket_bind(*sockfd, addr, parsed_port);

        // Initialize peer address length to zero
        context.peer_addr_len = 0;
    }
    else
    {
        // Connecting mode
        printf("Attempting to connect to %s:%s...\n", ip_address, port);
        context.is_host = false;

        // Convert address and set as destination
        convert_address(ip_address, addr);

        // Set the port
        if(addr->ss_family == AF_INET)
        {
            ((struct sockaddr_in *)addr)->sin_port = htons(parse_in_port_t("game", port));
        }
        else if(addr->ss_family == AF_INET6)
        {
            ((struct sockaddr_in6 *)addr)->sin6_port = htons(parse_in_port_t("game", port));
        }
        else
        {
            fprintf(stderr, "Unsupported address family\n");
            exit(EXIT_FAILURE);
        }

        // Store the host's address as the peer address
        memcpy(&context.peer_addr, addr, sizeof(struct sockaddr_storage));
        context.peer_addr_len = sizeof(struct sockaddr_storage);
    }

    return 0;
}

// void initializeGame()
//{
//     const char *ip_address = "127.0.0.1";    // Replace with actual IP
//     const char *port       = "12345";        // Replace with actual port
//
//     initializeNetwork(ip_address, port);
//     setStartingPositions();
//     setupNcurses();
// }
//
void setupNcurses(void)
{
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    nodelay(stdscr, TRUE);
    start_color();
    init_pair(1, COLOR_RED, COLOR_BLACK);     // Host color
    init_pair(2, COLOR_BLUE, COLOR_BLACK);    // Client color
    clear();
    refresh();
}

// Initializes the network connection with the provided IP address and port
// Converts IP address to sockaddr_storage and binds the socket to the specified port for UDP communication
void initializeNetwork(const char *ip_address, const char *port)
{
    struct sockaddr_storage addr;
    in_port_t               parsed_port;

    // Parse and convert the port string
    parsed_port = parse_in_port_t("game", port);

    // Convert the provided IP address to sockaddr_storage
    convert_address(ip_address, &addr);

    // Create the UDP socket
    context.socket = socket_create(addr.ss_family, SOCK_DGRAM, 0);

    // Bind to the specified address and port
    socket_bind(context.socket, &addr, parsed_port);

    printf("Network initialized on %s:%s\n", ip_address, port);
}

// Parses the command-line arguments to extract the IP address and port
// Handles options and validates the number of arguments to correctly assign values for the IP address and port
void parse_arguments(int argc, char *argv[], const char **ip_address, char **port)
{
    int opt;
    int remaining_args;

    opterr = 0;

    while((opt = getopt(argc, argv, "h")) != -1)
    {
        switch(opt)
        {
            case 'h':
                usage(argv[0], EXIT_SUCCESS, NULL);
            case '?':
                usage(argv[0], EXIT_FAILURE, "Unknown option.");
            default:
                usage(argv[0], EXIT_FAILURE, NULL);
        }
    }

    remaining_args = argc - optind;

    if(remaining_args == 1)
    {
        // Only a port is provided
        *ip_address = NULL;
        *port       = argv[optind];
    }
    else if(remaining_args == 2)
    {
        // Both IP and port are provided
        *ip_address = argv[optind];
        *port       = argv[optind + 1];
    }
    else
    {
        usage(argv[0], EXIT_FAILURE, "Provide either a port to host or an IP and port to connect.");
    }
}

// Handles the parsed arguments for the network connection
// Ensures both IP address and port are provided for a successful connection
void handle_arguments(const char *binary_name, const char *ip_address, const char *port_str, in_port_t *port)
{
    if(ip_address == NULL)
    {
        usage(binary_name, EXIT_FAILURE, "The ip address is required.");
    }

    if(port_str == NULL)
    {
        usage(binary_name, EXIT_FAILURE, "The port is required.");
    }

    *port = parse_in_port_t(binary_name, port_str);
}

// Parses the port string to convert it to a valid `in_port_t` type
// Converts the port string to a numerical `in_port_t` type, ensuring the value is within valid range
in_port_t parse_in_port_t(const char *binary_name, const char *str)
{
    char     *endptr;
    uintmax_t parsed_value;

    parsed_value = strtoumax(str, &endptr, BASE_TEN);
    errno        = 0;
    if(errno != 0)
    {
        perror("Error parsing in_port_t");
        exit(EXIT_FAILURE);
    }

    // Check if there are any non-numeric characters in the input string
    if(*endptr != '\0')
    {
        usage(binary_name, EXIT_FAILURE, "Invalid characters in input.");
    }

    // Check if the parsed value is within the valid range for in_port_t
    if(parsed_value > UINT16_MAX)
    {
        usage(binary_name, EXIT_FAILURE, "in_port_t value out of range.");
    }

    return (in_port_t)parsed_value;
}

// Converts an IP address (IPv4/IPv6) string into a sockaddr_storage structure
// Translates a given IP address to a network address structure for later socket communication
void convert_address(const char *address, struct sockaddr_storage *addr)
{
    memset(addr, 0, sizeof(*addr));

    if(inet_pton(AF_INET, address, &(((struct sockaddr_in *)addr)->sin_addr)) == 1)
    {
        addr->ss_family = AF_INET;
    }
    else if(inet_pton(AF_INET6, address, &(((struct sockaddr_in6 *)addr)->sin6_addr)) == 1)
    {
        addr->ss_family = AF_INET6;
    }
    else
    {
        fprintf(stderr, "%s is not an IPv4 or an IPv6 address\n", address);
        exit(EXIT_FAILURE);
    }
}

// Creates a UDP socket for communication
// Creates a socket of the specified type and domain (IPv4/IPv6) for communication
int socket_create(int domain, int type, int protocol)
{
    int sockfd;

    sockfd = socket(domain, type, protocol);

    if(sockfd == -1)
    {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    return sockfd;
}

// Binds the socket to the specified address and port
void socket_bind(int sockfd, struct sockaddr_storage *addr, in_port_t port)
{
    char      addr_str[INET6_ADDRSTRLEN];
    socklen_t addr_len;
    void     *vaddr;
    in_port_t net_port;

    net_port = htons(port);

    if(addr->ss_family == AF_INET)
    {
        struct sockaddr_in *ipv4_addr;

        ipv4_addr           = (struct sockaddr_in *)addr;
        addr_len            = sizeof(*ipv4_addr);
        ipv4_addr->sin_port = net_port;
        vaddr               = (void *)&(((struct sockaddr_in *)addr)->sin_addr);
    }
    else if(addr->ss_family == AF_INET6)
    {
        struct sockaddr_in6 *ipv6_addr;

        ipv6_addr            = (struct sockaddr_in6 *)addr;
        addr_len             = sizeof(*ipv6_addr);
        ipv6_addr->sin6_port = net_port;
        vaddr                = (void *)&(((struct sockaddr_in6 *)addr)->sin6_addr);
    }
    else
    {
        fprintf(stderr, "Internal error: addr->ss_family must be AF_INET or AF_INET6, was: %d\n", addr->ss_family);
        exit(EXIT_FAILURE);
    }

    if(inet_ntop(addr->ss_family, vaddr, addr_str, sizeof(addr_str)) == NULL)
    {
        perror("inet_ntop");
        exit(EXIT_FAILURE);
    }

    printf("Binding to: %s:%u\n", addr_str, port);

    if(bind(sockfd, (struct sockaddr *)addr, addr_len) == -1)
    {
        perror("Binding failed");
        fprintf(stderr, "Error code: %d\n", errno);
        exit(EXIT_FAILURE);
    }

    printf("Bound to socket: %s:%u\n", addr_str, port);
}

void socket_close(int sockfd)
{
    if(close(sockfd) == -1)
    {
        perror("Error closing socket");
        exit(EXIT_FAILURE);
    }
}

// UDP communication functions
void sendUDPMessage(int sockfd, const struct sockaddr_storage *dest_addr, socklen_t addr_len, const char *message)
{
    if(sendto(sockfd, message, strlen(message), 0, (const struct sockaddr *)dest_addr, addr_len) == -1)
    {
        perror("Failed to send message");
        exit(EXIT_FAILURE);
    }
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"

ssize_t receiveUDPMessage(int sockfd, struct sockaddr_storage *source_addr, socklen_t *addr_len, char *buffer, size_t buffer_size)
{
    ssize_t bytes_received;

    // Initialize addr_len if needed
    if(addr_len && *addr_len == 0)
    {
        *addr_len = sizeof(struct sockaddr_storage);
    }

    // Wait to receive a message
    bytes_received = recvfrom(sockfd, buffer, buffer_size - 1, 0, (struct sockaddr *)source_addr, addr_len);

    if(bytes_received == -1)
    {
        perror("recvfrom");
        return -1;    // Return an error instead of exiting the program
    }

    // Null-terminate the received data
    buffer[bytes_received] = '\0';

    // For the host, store the client's address after receiving the first message
    if(context.is_host && context.peer_addr_len == 0 && addr_len != NULL && source_addr != NULL)
    {
        memcpy(&context.peer_addr, source_addr, *addr_len);
        context.peer_addr_len = *addr_len;
    }

    // Process the received message
    handleReceivedPacket(buffer);

    return bytes_received;
}

#pragma GCC diagnostic pop

char *createPacket(int x, int y, const char *game_state)
{
    static char packet[BUFFER_SIZE];
    snprintf(packet, sizeof(packet), "%d,%d|%s", x, y, game_state);
    return packet;
}

void handleReceivedPacket(const char *packet)
{
    updateRemoteDot(packet);    // Example of how packet might be handled
}

void updateRemoteDot(const char *packet)
{
    char       *packet_copy;
    const char *token;
    char       *save_ptr;    // For strtok_r
    char       *endptr;
    long        x_long;
    long        y_long;
    int         x;
    int         y;
    char        game_state[BUFFER_SIZE];

    packet_copy = strdup(packet);
    if(packet_copy == NULL)
    {
        perror("strdup");
        return;
    }

    // First token
    token = strtok_r(packet_copy, ",|", &save_ptr);
    if(token == NULL)
    {
        fprintf(stderr, "Invalid packet format: %s\n", packet);
        free(packet_copy);
        return;
    }

    x_long = strtol(token, &endptr, TEN);
    if(*endptr != '\0')
    {
        fprintf(stderr, "Invalid X coordinate: %s\n", token);
        free(packet_copy);
        return;
    }

    // Second token
    token = strtok_r(NULL, ",|", &save_ptr);
    if(token == NULL)
    {
        fprintf(stderr, "Invalid packet format: %s\n", packet);
        free(packet_copy);
        return;
    }

    y_long = strtol(token, &endptr, TEN);
    if(*endptr != '\0')
    {
        fprintf(stderr, "Invalid Y coordinate: %s\n", token);
        free(packet_copy);
        return;
    }

    // Third token
    token = strtok_r(NULL, "|", &save_ptr);
    if(token == NULL)
    {
        fprintf(stderr, "Invalid packet format: %s\n", packet);
        free(packet_copy);
        return;
    }

    strncpy(game_state, token, sizeof(game_state) - 1);
    game_state[sizeof(game_state) - 1] = '\0';

    x = (int)x_long;
    y = (int)y_long;

    if(context.is_host)
    {
        // We are the host, the remote player is the client
        context.clientx = x;
        context.clienty = y;

        // After receiving the client's position, send our position back
        sendPositionUpdate();
    }
    else
    {
        // We are the client, the remote player is the host
        context.hostx = x;
        context.hosty = y;
    }

    free(packet_copy);
}

// Utility function
_Noreturn void usage(const char *program_name, int exit_code, const char *message)
{
    if(message)
    {
        fprintf(stderr, "%s\n", message);
    }
    fprintf(stderr, "Usage: %s [-h] <IP address> <port>\n", program_name);
    fprintf(stderr, "Options:\n  -h  Display this help message\n");
    exit(exit_code);
}

// Function to generate random coordinates
//void generateRandomCoordinates(int *x, int *y)
//{
//    *x = arc4random() % GAME_GRID_SIZE;    // Random x between 0 and 99
//    *y = arc4random() % GAME_GRID_SIZE;    // Random y between 0 and 99
//}

// Set starting positions for both players
void setStartingPositions(void)
{
    if(context.is_host)
    {
        context.hostx   = GAME_GRID_SIZE / 2;
        context.hosty   = GAME_GRID_SIZE / 2;
        context.clientx = -1;    // Unknown position
        context.clienty = -1;
    }
    else
    {
        context.clientx = GAME_GRID_SIZE / 2;
        context.clienty = GAME_GRID_SIZE / 2;
        context.hostx   = -1;    // Unknown position
        context.hosty   = -1;
    }
}

// Clean up ncurses
void cleanupNcurses(void)
{
    endwin();    // End ncurses mode
}

// Draws the dots as X's
void drawDot(int x, int y, int color_pair)
{
    attron(COLOR_PAIR(color_pair));
    mvaddch(y % LINES, x % COLS, 'X');
    attroff(COLOR_PAIR(color_pair));
}

// Read user input
int getUserInput(void)
{
    return getch();    // Non-blocking input
}

// Update local dot based on input
void updateLocalDot(int ch)
{
    int *x;
    int *y;
    if(context.is_host)
    {
        x = &context.hostx;
        y = &context.hosty;
    }
    else
    {
        x = &context.clientx;
        y = &context.clienty;
    }

    switch(ch)
    {
        case KEY_UP:
            *y = (*y - 1 + GAME_GRID_SIZE) % GAME_GRID_SIZE;
            break;
        case KEY_DOWN:
            *y = (*y + 1) % GAME_GRID_SIZE;
            break;
        case KEY_LEFT:
            *x = (*x - 1 + GAME_GRID_SIZE) % GAME_GRID_SIZE;
            break;
        case KEY_RIGHT:
            *x = (*x + 1) % GAME_GRID_SIZE;
            break;
        default:
            break;
    }
}

// Clear the screen
void clearScreen(void)
{
    clear();
    refresh();
}

// Sends update of dot position
void sendPositionUpdate(void)
{
    int         x;
    int         y;
    const char *packet;
    if(context.is_host)
    {
        x = context.hostx;
        y = context.hosty;
    }
    else
    {
        x = context.clientx;
        y = context.clienty;
    }
    packet = createPacket(x, y, "update");

    // Only send if we have a valid peer address
    if(context.peer_addr_len > 0)
    {
        if(sendto(context.socket, packet, strlen(packet), 0, (struct sockaddr *)&context.peer_addr, context.peer_addr_len) == -1)
        {
            perror("Failed to send message");
            // Handle error appropriately, possibly exit or set a flag
        }
    }
}

void updateScreen(void)
{
    clear();    // Clear the screen

    // Draw the grid background
    for(int y = 0; y < GAME_GRID_SIZE; ++y)
    {
        for(int x = 0; x < GAME_GRID_SIZE; ++x)
        {
            mvaddch(y, x, '.');    // Move to position (y, x) and add a dot
        }
    }

    // Draw local player's dot
    if(context.is_host)
    {
        drawDot(context.hostx, context.hosty, 1);    // Host dot
    }
    else
    {
        drawDot(context.clientx, context.clienty, 2);    // Client dot
    }

    // Draw remote player's dot if known
    if(context.is_host && context.clientx >= 0 && context.clienty >= 0)
    {
        drawDot(context.clientx, context.clienty, 2);    // Client dot
    }
    else if(!context.is_host && context.hostx >= 0 && context.hosty >= 0)
    {
        drawDot(context.hostx, context.hosty, 1);    // Host dot
    }

    refresh();    // Refresh the screen to display changes
}

// Receives updates of dot position
void receivePositionUpdate(void)
{
    char                    buffer[BUFFER_SIZE];
    struct sockaddr_storage source_addr;
    socklen_t               addr_len = sizeof(source_addr);

    ssize_t bytes = receiveUDPMessage(context.socket, &source_addr, &addr_len, buffer, sizeof(buffer));
    if(bytes > 0)
    {
        updateRemoteDot(buffer);
        updateScreen();
    }
}

// Update the screen to show both local and remote dots
void handleInput(void)
{
    int ch = getch();
    if(ch != ERR)
    {
        updateLocalDot(ch);
        sendPositionUpdate();
        updateScreen();
    }
}

// Display an error message and exit
_Noreturn void errorMessage(const char *msg)
{
    cleanupNcurses();
    fprintf(stderr, "Error: %s\n", msg);
    exit(EXIT_FAILURE);
}

// Main entry point of the program
// Parses the command-line arguments, initializes the socket and game loop
// Handles communication between host and client based on the mode (host or client)
int main(int argc, char *argv[])
{
    struct sockaddr_storage addr;
    socklen_t               addr_len = sizeof(addr);
    int                     sockfd;
    char                    buffer[BUFFER_SIZE];
    const char             *ip_address = NULL;
    char                   *port       = NULL;

    uint32_t randomSeed = arc4random();
    srand(randomSeed);

    memset(&context, 0, sizeof(Context));
    setupNcurses();

    parse_arguments(argc, argv, &ip_address, &port);

    sockfd         = socket_create(AF_INET, SOCK_DGRAM, 0);
    context.socket = sockfd;

    setupConnection(&sockfd, &addr, ip_address, port);

    setStartingPositions();
    sendPositionUpdate();
    updateScreen();

    signal(SIGINT, handle_signal);

    while(!quit_flag)
    {
        int            activity;
        int            max_fd;
        fd_set         readfds;
        struct timeval tv;

        FD_ZERO(&readfds);
        FD_SET(sockfd, &readfds);
        FD_SET(STDIN_FILENO, &readfds);

        tv.tv_sec  = 0;
        tv.tv_usec = SELECT_TIMEOUT_USEC;

        max_fd   = sockfd > STDIN_FILENO ? sockfd : STDIN_FILENO;
        activity = select(max_fd + 1, &readfds, NULL, NULL, &tv);

        if(activity > 0)
        {
            if(FD_ISSET(sockfd, &readfds))
            {
                ssize_t bytes = receiveUDPMessage(sockfd, &addr, &addr_len, buffer, sizeof(buffer));
                if(bytes > 0)
                {
                    if(context.is_host && context.peer_addr_len == 0)
                    {
                        memcpy(&context.peer_addr, &addr, addr_len);
                        context.peer_addr_len = addr_len;
                    }
                    updateRemoteDot(buffer);
                    updateScreen();
                }
            }

            if(FD_ISSET(STDIN_FILENO, &readfds))
            {
                handleInput();
            }
        }
    }

    cleanupNcurses();
    socket_close(sockfd);
    printf("Exiting...\n");
    return 0;
}

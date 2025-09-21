#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <getopt.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <termios.h>
#include <fcntl.h>

#define PORT 65432
#define DEVICE "/dev/ttyUSB0"
#define BAUD_RATE 115200

#define RX_BUFFER_SIZE 2
#define TX_BUFFER_SIZE 256

static int tcp_socket_fd;
static int serial_port_fd;
static bool verbose;

static speed_t baudrate_to_speed_t(int baudrate)
{
    switch (baudrate) {
        case 50:
            return B50;
        case 75:
            return B75;
        case 110:
            return B110;
        case 134:
            return B134;
        case 150:
            return B150;
        case 200:
            return B200;
        case 300:
            return B300;
        case 600:
            return B600;
        case 1200:
            return B1200;
        case 1800:
            return B1800;
        case 2400:
            return B2400;
        case 4800:
            return B4800;
        case 9600:
            return B9600;
        case 19200:
            return B19200;
        case 38400:
            return B38400;
        case 57600:
            return B57600;
        case 115200:
            return B115200;
        case 230400:
            return B230400;
        case 460800:
            return B460800;
        case 500000:
            return B500000;
        case 576000:
            return B576000;
        case 921600:
            return B921600;
        case 1000000:
            return B1000000;
        case 1152000:
            return B1152000;
        case 1500000:
            return B1500000;
        case 2000000:
            return B2000000;
        case 2500000:
            return B2500000;
        case 3000000:
            return B3000000;
        case 3500000:
            return B3500000;
        case 4000000:
            return B4000000;
        default:
            fprintf(stderr, "Error: Invalid or unsupported baud rate: %d\n", baudrate);
            return (speed_t)-1;
    }
}

// Function to configure the serial port
static int configure_serial_port(int fd, int baud_rate)
{
    struct termios tty;
    if (tcgetattr(fd, &tty) != 0) {
        perror("Error from tcgetattr");
        return -1;
    }

    // Set baud rate
    speed_t b = baudrate_to_speed_t(baud_rate);
    cfsetospeed(&tty, b);
    cfsetispeed(&tty, b);

    // Set other serial port settings
    tty.c_cflag &= ~PARENB;      // No parity
    tty.c_cflag &= ~CSTOPB;      // 1 stop bit
    tty.c_cflag &= ~CSIZE;       // Clear size bits
    tty.c_cflag |= CS8;          // 8 data bits
    tty.c_cflag &= ~CRTSCTS;     // Disable hardware flow control
    tty.c_cflag |= CREAD | CLOCAL; // Enable reading and ignore modem controls

    tty.c_lflag &= ~ICANON;      // Disable canonical mode
    tty.c_lflag &= ~ECHO;        // Disable echo
    tty.c_lflag &= ~ECHOE;       // Disable erase
    tty.c_lflag &= ~ECHONL;      // Disable newline echo
    tty.c_lflag &= ~ISIG;        // Disable signal characters

    tty.c_iflag &= ~(IXON | IXOFF | IXANY); // Disable software flow control
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL); // Disable special handling

    tty.c_oflag &= ~OPOST;       // Prevent special interpretation of output bytes
    tty.c_oflag &= ~ONLCR;       // Prevent newline to carriage return conversion

    tty.c_cc[VTIME] = 10;        // Wait for up to 1 second (10 * 0.1s)
    tty.c_cc[VMIN] = 1;          // Non-blocking read

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        perror("Error from tcsetattr");
        return -1;
    }

    return 0;
}

// Thread to read from TCP socket and write to serial port
static void *tcp_to_serial_thread(void* arg)
{
    char buffer[RX_BUFFER_SIZE];
    ssize_t bytes_read;
    while ((bytes_read = recv(tcp_socket_fd, buffer, sizeof(buffer), 0)) > 0) {
        if (verbose) {
            printf("%x %x\n", buffer[0], buffer[1]);
        }
        if (write(serial_port_fd, buffer, bytes_read) != bytes_read) {
            perror("write");
            exit(1);
        }
    }

    return NULL;
}

// Thread to read from serial port and write to TCP socket
static void *serial_to_tcp_thread(void* arg)
{
    char buffer[TX_BUFFER_SIZE];
    ssize_t bytes_read;
    while ((bytes_read = read(serial_port_fd, buffer, sizeof(buffer))) > 0) {
#if 0
        if (send(tcp_socket_fd, buffer, bytes_read, 0) != bytes_read) {
            perror("write");
            exit(1);
        }
#else
        if (write(STDOUT_FILENO, buffer, bytes_read) != bytes_read) {
            perror("write");
            exit(1);
        }
#endif
    }

    return NULL;
}

static void usage(const char *prog_name)
{
    fprintf(stderr, "Usage: %s -p <port> -d <device> -b <baud>\n\n", prog_name);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -p, --port <number>     Specify the port number (default %d).\n", PORT);
    fprintf(stderr, "  -d, --device <path>     Specify the serial device (default %s).\n", DEVICE);
    fprintf(stderr, "  -b, --baud <rate>       Specify the baud rate (default %d).\n", BAUD_RATE);
    fprintf(stderr, "  -h, --help              Display this help message and exit.\n");
}

int main(int argc, char *argv[])
{
    int port = PORT;
    char *device = DEVICE;
    int baud = BAUD_RATE;
    int c;
    int option_index = 0;
    const char *short_options = "hp:d:b:";
    static const struct option long_options[] = {
        {"port",    required_argument, 0, 'p'},
        {"device",  required_argument, 0, 'd'},
        {"baud",    required_argument, 0, 'b'},
        {"verbose", no_argument, 0, 'v'},
        {"help",                    0, 0,   0},
        {0,         0,                 0,  0 } // Marks the end of the array
    };

    while ((c = getopt_long(argc, argv, short_options, long_options, &option_index)) != -1) {
        switch (c) {
            case 'p':
                port = atoi(optarg);
                break;
            case 'd':
                device = optarg;
                break;
            case 'b':
                baud = atoi(optarg);
                break;
            case 'v':
                verbose = true;
                break;
            case 'h':
            default:
                usage(argv[0]);
                exit(1);
        }
    }

    serial_port_fd = open(device, O_RDWR | O_NOCTTY | O_SYNC);
    if (serial_port_fd < 0) {
        perror("Error opening serial port");
        return 1;
    }

    if (configure_serial_port(serial_port_fd, baud) < 0) {
        return 1;
    }

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("Socket creation failed");
        return 1;
    }

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        close(server_fd);
        close(serial_port_fd);
        return 1;
    }

    if (listen(server_fd, 1) < 0) {
        perror("Listen failed");
        close(server_fd);
        close(serial_port_fd);
        return 1;
    }

    printf("Server listening on port %d and forwarding to %s...\n", port, device);

    int addrlen = sizeof(address);
    int new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen);
    if (new_socket < 0) {
        perror("Accept failed");
        close(server_fd);
        close(serial_port_fd);
        return 1;
    }
    tcp_socket_fd = new_socket;

    printf("Connection accepted from %s:%d. Starting forwarding...\n", inet_ntoa(address.sin_addr), ntohs(address.sin_port));

    pthread_t tcp_thread, serial_thread;
    pthread_create(&tcp_thread, NULL, tcp_to_serial_thread, NULL);
    pthread_create(&serial_thread, NULL, serial_to_tcp_thread, NULL);

    pthread_join(tcp_thread, NULL);
    pthread_join(serial_thread, NULL);

    close(new_socket);
    close(server_fd);
    close(serial_port_fd);

    printf("Connection closed. Exiting.\n");
    return 0;
}

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <getopt.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <linux/input.h>
#include <netdb.h>

#define SERVER_HOST "127.0.0.1"          // Replace with the server's IP address
#define SERVER_PORT "65432"              // The port the server is listening on
#define EVENT_DEVICE "/dev/input/event0" // The input event file to listen to

#define PRESS_IDENTIFIER 254
#define RELEASE_IDENTIFIER 255

void usage(const char *prog_name) {
    printf("Usage: %s [options]\n", prog_name);
    printf("Options:\n");
    printf("  -h, --host <hostname>    Specify the hostname (default %s)\n", SERVER_HOST);
    printf("  -p, --port <port>        Specify the port (default %s)\n", SERVER_PORT);
    printf("  -d, --device <device>    Specify the device name (default %s)\n", EVENT_DEVICE);
    printf("  -v, --verbose            Enable verbose output\n");
    printf("\n");
}

static char *host = SERVER_HOST;
static char *port = SERVER_PORT;
static char *device = EVENT_DEVICE;
static bool verbose = false;

int main(int argc, char *argv[]) {
    int opt;
    static struct option long_options[] = {
        {"host",    required_argument, 0, 'h'},
        {"port",    required_argument, 0, 'p'},
        {"device",  required_argument, 0, 'd'},
        {"verbose", no_argument,       0, 'v'},
        {0, 0, 0, 0} // End of array marker
    };
    const char *short_options = "h:p:d:v";
    int long_index = 0;

    while ((opt = getopt_long(argc, argv, short_options, long_options, &long_index)) != -1) {
        switch (opt) {
            case 'h':
                host = optarg;
                break;
            case 'p':
                port = optarg;
                break;
            case 'd':
                device = optarg;
                break;
            case 'v':
                verbose = true;
                break;
            default:
                usage(argv[0]);
                exit(1);
        }
    }

    // Open the keyboard input device file
    int event_fd = open(device, O_RDONLY);
    if (event_fd < 0) {
        perror("Failed to open input device");
        return 1;
    }

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *result;
    int status;
    if ((status = getaddrinfo(host, port, &hints, &result)) != 0) {
        fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
        close(event_fd);
        return -1;
    }

    // Create the TCP socket
    int sock;
    if ((sock = socket(result->ai_family, result->ai_socktype, result->ai_protocol)) < 0) {
        perror("Socket creation error");
        close(event_fd);
        freeaddrinfo(result);
        return 1;
    }

    // Connect to the server, use the first result
    if (connect(sock, result->ai_addr, result->ai_addrlen) == -1) {
        perror("Connection Failed");
        close(event_fd);
        close(sock);
        freeaddrinfo(result);
        return 1;
    }

    freeaddrinfo(result);

    while (1) {
        struct input_event ev;

        // Read a single input event
        if (read(event_fd, &ev, sizeof(struct input_event)) < 0) {
            perror("Error reading input event");
            break;
        }

        // Check if the event is a key event
        if (ev.type == EV_KEY) {
            char buffer[2];
            int code = ev.code;

            if (ev.value == 1) { // Key press
                if (verbose) {
                    printf("Key Down: %d\n", code);
                }
                buffer[0] = PRESS_IDENTIFIER;
            } else if (ev.value == 0) { // Key release
                if (verbose) {
                    printf("Key Up: %d\n", code);
                }
                buffer[0] = RELEASE_IDENTIFIER;
            } else if (ev.value == 2) { // Key auto repeat
                // Ignore
                continue;
            } else {
                printf("Unknown event: %d\n", ev.value);
                continue;
            }

            // Validate key code fits in a byte
            if (code > 255) {
                printf("Key code %d too large, skipping\n", code);
                continue;
            }
            buffer[1] = (char)code;

            if (send(sock, buffer, sizeof(buffer), 0) != sizeof(buffer)) {
                perror("Failed to send data");
                break;
            }
        }
    }

    // Cleanup
    close(event_fd);
    close(sock);
    return 0;
}

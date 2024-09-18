#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <netinet/in.h>
#include <errno.h>  // Include errno for error handling
#include "protocol.h"

#define BUFFER_SIZE 1024
#define TIMEOUT_SEC 2
#define MAX_RETRIES 2

// Uncomment the next line to enable debug mode
// #define DEBUG

void send_message(int sockfd, struct sockaddr *server_addr, socklen_t addr_len, void *msg, size_t msg_len) {
    if (sendto(sockfd, msg, msg_len, 0, server_addr, addr_len) < 0) {
        perror("ERROR: Sending message");
        exit(1);
    }
}

int receive_message(int sockfd, void *buffer, size_t buffer_size, struct sockaddr *server_addr, socklen_t *addr_len) {
    int n = recvfrom(sockfd, buffer, buffer_size, 0, server_addr, addr_len);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // Timeout occurred
            return 0; // Indicate timeout
        } else {
            perror("ERROR: Receiving message");
            return -1;
        }
    }
    return n;
}

int main(int argc, char *argv[]) {
    int sockfd;
    struct sockaddr_storage server_addr;  // Support for both IPv4 and IPv6
    socklen_t addr_len;
    struct calcMessage message;     // Use calcMessage structure from protocol.h
    struct calcProtocol response;   // Use calcProtocol structure from protocol.h

    // Check for correct usage
    if (argc != 2) {
        printf("Usage: %s <hostname>:<port>\n", argv[0]);
        return 1;
    }

    // Extract hostname and port from command-line argument
    char *hostname = strtok(argv[1], ":");
    char *port_str = strtok(NULL, ":");
    if (hostname == NULL || port_str == NULL) {
        printf("ERROR: Invalid input format. Expected <hostname>:<port>\n");
        return 1;
    }

    int port = atoi(port_str);

    // Use getaddrinfo to resolve hostname
    struct addrinfo hints, *res, *rp;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;  // Allow both IPv4 and IPv6
    hints.ai_socktype = SOCK_DGRAM;  // UDP

    int status = getaddrinfo(hostname, port_str, &hints, &res);
    if (status != 0) {
        fprintf(stderr, "ERROR: getaddrinfo: %s\n", gai_strerror(status));
        return 1;
    }

    // Create UDP socket
    for (rp = res; rp != NULL; rp = rp->ai_next) {
        sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sockfd == -1) continue;

        if (connect(sockfd, rp->ai_addr, rp->ai_addrlen) != -1) break; // Success

        close(sockfd);
    }

    if (rp == NULL) {
        fprintf(stderr, "ERROR: Could not connect to %s\n", hostname);
        freeaddrinfo(res);
        return 1;
    }

    // Prepare initial calcMessage
    message.type = htons(22); // Client to server, binary protocol
    message.message = htonl(0); // N/A
    message.protocol = htons(17); // UDP protocol
    message.major_version = htons(1);
    message.minor_version = htons(0);

    printf("Host %s, and port %d.\n", hostname, port);

    // Set timeout for socket
    struct timeval timeout = {TIMEOUT_SEC, 0};
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    // Send initial message with retries
    int retries = 0;
    do {
        send_message(sockfd, rp->ai_addr, rp->ai_addrlen, &message, sizeof(message));

        // Wait for server response
        addr_len = sizeof(server_addr); // Update addr_len to the actual size
        int n = receive_message(sockfd, &response, sizeof(response), (struct sockaddr *)&server_addr, &addr_len);
        if (n > 0) {
            if (ntohs(response.type) == 2 && ntohl(((struct calcMessage*)&response)->message) == 2) {
                printf("ERROR: Server sent NOT OK message\n");
                close(sockfd);
                freeaddrinfo(res);
                return 1;
            } else if (ntohs(response.type) == 1) {
                break; // Received calcProtocol message
            }
        } else if (n == 0) {
            // Timeout occurred, increment retries
            retries++;
        } else {
            // An actual error occurred
            close(sockfd);
            freeaddrinfo(res);
            return 1;
        }
    } while (retries <= MAX_RETRIES);

    if (retries > MAX_RETRIES) {
        printf("ERROR TO\n");  // Modified to match test script
        close(sockfd);
        freeaddrinfo(res);
        return 1;
    }

    // Perform calculation
    int int_result = 0;
    double float_result = 0.0;
    int is_float_operation = 0; // Flag to determine if it's a floating-point operation

    switch (ntohl(response.arith)) {
        case 1: // Add
            int_result = ntohl(response.inValue1) + ntohl(response.inValue2);
            break;
        case 2: // Subtract
            int_result = ntohl(response.inValue1) - ntohl(response.inValue2);
            break;
        case 3: // Multiply
            int_result = ntohl(response.inValue1) * ntohl(response.inValue2);
            break;
        case 4: // Divide
            int_result = ntohl(response.inValue1) / ntohl(response.inValue2);
            break;
        case 5: // Floating-point Add
            is_float_operation = 1;
            float_result = response.flValue1 + response.flValue2;
            break;
        case 6: // Floating-point Subtract
            is_float_operation = 1;
            float_result = response.flValue1 - response.flValue2;
            break;
        case 7: // Floating-point Multiply
            is_float_operation = 1;
            float_result = response.flValue1 * response.flValue2;
            break;
        case 8: // Floating-point Divide
            is_float_operation = 1;
            float_result = response.flValue1 / response.flValue2;
            break;
        default:
            printf("ERROR: Unsupported operation\n");
            close(sockfd);
            freeaddrinfo(res);
            return 1;
    }

#ifdef DEBUG
    if (is_float_operation) {
        printf("Calculated the floating-point result to %.8f\n", float_result);
    } else {
        printf("Calculated the integer result to %d\n", int_result);
    }
#endif

    // Prepare and send calcProtocol response
    response.type = htons(2); // Set type to 2
    response.major_version = htons(1);
    response.minor_version = htons(0);

    if (is_float_operation) {
        response.flResult = float_result; // No conversion needed for doubles
    } else {
        response.inResult = htonl(int_result);
    }

    // Send result to server with retries
    retries = 0;
    do {
        send_message(sockfd, rp->ai_addr, rp->ai_addrlen, &response, sizeof(response));

        // Wait for server final response
        addr_len = sizeof(server_addr); // Update addr_len to the actual size
        int n = receive_message(sockfd, &message, sizeof(message), (struct sockaddr *)&server_addr, &addr_len);
        if (n > 0 && ntohs(message.type) == 2) {
            if (ntohl(message.message) == 1) {
                if (is_float_operation) {
                    printf("OK (myresult=%.8f)\n", float_result);
                } else {
                    printf("OK (myresult=%d)\n", int_result);
                }
                break;
            } else {
                if (is_float_operation) {
                    printf("ERROR (myresult=%.8f)\n", float_result);
                } else {
                    printf("ERROR (myresult=%d)\n", int_result);
                }
                break;
            }
        } else if (n == 0) {
            // Timeout occurred, increment retries
            retries++;
        } else {
            // An actual error occurred
            close(sockfd);
            freeaddrinfo(res);
            return 1;
        }
    } while (retries <= MAX_RETRIES);

    if (retries > MAX_RETRIES) {
        printf("ERROR TO\n");  // Modified to match test script
    }

    close(sockfd);
    freeaddrinfo(res);
    return 0;
}

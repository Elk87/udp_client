#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include "protocol.h"

#define SERVER_IP "13.53.76.30"
#define SERVER_PORT 5000
#define BUFFER_SIZE 1024
#define TIMEOUT_SEC 2
#define MAX_RETRIES 2

// Uncomment the next line to enable debug mode
// #define DEBUG

void send_message(int sockfd, struct sockaddr_in *server_addr, socklen_t addr_len, void *msg, size_t msg_len) {
    if (sendto(sockfd, msg, msg_len, 0, (struct sockaddr *)server_addr, addr_len) < 0) {
        perror("ERROR: Sending message");
        exit(1);
    }
}

int receive_message(int sockfd, void *buffer, size_t buffer_size, struct sockaddr_in *server_addr, socklen_t *addr_len) {
    int n = recvfrom(sockfd, buffer, buffer_size, 0, (struct sockaddr *)server_addr, addr_len);
    if (n < 0) {
        perror("ERROR: Receiving message");
    }
    return n;
}

int main(int argc, char *argv[]) {
    int sockfd, retries = 0;
    struct sockaddr_in server_addr;
    socklen_t addr_len = sizeof(server_addr);
    struct calcMessage message;
    struct calcProtocol response;

    if (argc != 2) {
        printf("Usage: %s <hostname>:<port>\n", argv[0]);
        return 1;
    }

    // Create UDP socket
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("ERROR: Cannot create socket");
        return 1;
    }

    // Setup server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0) {
        perror("ERROR: Invalid server IP address");
        return 1;
    }

    // Set timeout for socket
    struct timeval timeout = {TIMEOUT_SEC, 0};
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    // Prepare initial calcMessage
    message.type = htons(22); // Client to server, binary protocol
    message.message = htonl(0); // N/A
    message.protocol = htons(17); // UDP protocol
    message.major_version = htons(1);
    message.minor_version = htons(0);

    printf("Host %s, and port %d.\n", SERVER_IP, SERVER_PORT);

    // Send initial message with retries
    do {
        send_message(sockfd, &server_addr, addr_len, &message, sizeof(message));

        // Wait for server response
        int n = receive_message(sockfd, &response, sizeof(response), &server_addr, &addr_len);
        if (n > 0) {
            if (ntohs(response.type) == 2 && ntohl(((struct calcMessage*)&response)->message) == 2) {
                printf("ERROR: Server sent NOT OK message\n");
                close(sockfd);
                return 1;
            } else if (ntohs(response.type) == 1) {
                break; // Received calcProtocol message
            }
        }
        retries++;
    } while (retries <= MAX_RETRIES);

    if (retries > MAX_RETRIES) {
        printf("ERROR: Server did not reply\n");
        close(sockfd);
        return 1;
    }

    // Perform calculation
    int result;
    switch (ntohl(response.arith)) {
        case 1: // Add
            result = ntohl(response.inValue1) + ntohl(response.inValue2);
            break;
        case 2: // Subtract
            result = ntohl(response.inValue1) - ntohl(response.inValue2);
            break;
        case 3: // Multiply
            result = ntohl(response.inValue1) * ntohl(response.inValue2);
            break;
        case 4: // Divide
            result = ntohl(response.inValue1) / ntohl(response.inValue2);
            break;
        default:
            printf("ERROR: Unsupported operation\n");
            close(sockfd);
            return 1;
    }

#ifdef DEBUG
    printf("Calculated the result to %d\n", result);
#endif

    // Prepare and send calcProtocol response
    response.inResult = htonl(result);
    send_message(sockfd, &server_addr, addr_len, &response, sizeof(response));

    // Wait for server final response
    retries = 0;
    do {
        int n = receive_message(sockfd, &message, sizeof(message), &server_addr, &addr_len);
        if (n > 0 && ntohs(message.type) == 2) {
            if (ntohl(message.message) == 1) {
                printf("OK (myresult=%d)\n", result);
                break;
            } else {
                printf("ERROR (myresult=%d)\n", result);
                break;
            }
        }
        retries++;
    } while (retries <= MAX_RETRIES);

    if (retries > MAX_RETRIES) {
        printf("ERROR: Server did not reply after sending result\n");
    }

    close(sockfd);
    return 0;
}

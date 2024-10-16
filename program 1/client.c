#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>

#define BUFFER_SIZE 1024
struct server_info{
    struct sockaddr_in server_addr;
    socklen_t addr_len;
};

int send_message(char* buffer, int sockfd, struct server_info* s_in){
        
        sendto(sockfd, buffer, strlen(buffer), 0, (struct sockaddr *)&(s_in->server_addr), s_in->addr_len);

        // receive response
        memset(buffer, 0, BUFFER_SIZE);
        int n = recvfrom(sockfd, buffer, BUFFER_SIZE, 0, (struct sockaddr *)&(s_in->server_addr), &(s_in->addr_len));
        if (n < 0) {
            perror("rcv failed");
        } else {
            printf("%s\n", buffer);
        }
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <server host> <port> <username>\n", argv[0]);
        exit(1);
    }

    char *server_host = argv[1];
    int port = atoi(argv[2]);
    char *username = argv[3];

    int sockfd;
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE];
    
    // Create UDP socket
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    // server info struct
    struct server_info s_in;
    s_in.server_addr = server_addr;
    s_in.addr_len = sizeof(server_addr);

    // convert hostname to IP
    struct hostent *host = gethostbyname(server_host);
    if (!host) { // if error converting, host is unknown
        fprintf(stderr, "Error: Unknown host %s\n", server_host);
        exit(EXIT_FAILURE);
    }
    memcpy(&server_addr.sin_addr, host->h_addr, host->h_length);

    // start loop to accept user input
    while (1){
        char str[128];
        printf("> ");
        scanf("%s",str);
        char* tok = strtok(str, " ");
        if (strcmp(tok[0],"/exit") == 0){
            printf("Exit command entered. Exiting safely...\n");
            break;
        }
        else if (strcmp(tok[0],"/list") == 0){
            if (tok[1] != NULL){
                // construct channel list request and send to server
            }
        }
        else if (strcmp(tok[0],"/join") == 0){
            if (tok[1] != NULL){
                // construct packet with channel name and send to server
            }else{ // err
                printf("Please indicate channel name.\n");
            }
        }
        if (strcmp(tok[0],"/leave") == 0){
            if (tok[1] != NULL){
                // construct packet with channel name and send to server
            }else { // err
                printf("Please indicate channel name.\n");
            }
        }
        else if (strcmp(tok[0],"/who") == 0){
            if (tok[1] != NULL){
                // construct packet with channel name and send to server
            }else{ // err
                printf("Please indicate channel name.\n");
            }
        }
        else if (strcmp(tok[0],"/switch") == 0){
            if (tok[1] != NULL){
                // construct packet with channel name and send to server
            }else{ // err
                printf("Please indicate channel name.\n");
            }
        }
        else{
            // send message to server
        }
    }
    close(sockfd);
    return 0;
}

/*
client.c - for CS 432 Project 1
Nicholas Anthony
10/15/2024
*/
#include "duckchat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <pthread.h>

#define BUFFER_SIZE 1024
#define MAX_NUM_CHANNELS 10 //setting max number of subscribed channels

// globals
int sockfd;
struct sockaddr_in server_addr;
char active_channel[CHANNEL_MAX];
int subscribed_channels[MAX_NUM_CHANNELS][CHANNEL_MAX];
int subscr_count = 0; // subscribed channel count
char user_input[BUFFER_SIZE]; // save user input to display later

// functions
void send_req(void *req, size_t req_size);
void *receive(void *arg);
void login(char *username);
void logout();
void list_channels();
void switch_channels();
void join_channel(char *channel);
void leave_channel(char *channel);
void say(char *message);
void who(char *channel);
void display(const char *channel, const char *username, const char *message);

// struct (s)
struct server_info{
    struct sockaddr_in server_addr;
    socklen_t addr_len;
};

/* 
* BEGIN FUNCTION DEFINITIONS
*/
void send_req(void *req, size_t req_size) {
    int err = sendto(sockfd, req, req_size, 0, (struct sockaddr *)&server_addr, sizeof(struct sockaddr_in));  
    if (err < 0){
        perror("send_req");
        exit(1);
    }
}

// login with username
void login(char *user){
    struct request_login req;
    req.req_type = REQ_LOGIN;
    strncpy(req.req_username, user, USERNAME_MAX);
    send_req(&req, sizeof(req));
}

//logout and quit
void logout(){
    struct request_logout req;
    req.req_type = REQ_LOGOUT;
    send_req(&req, sizeof(req));
    close(sockfd);
    exit(EXIT_SUCCESS);
}

// list all current active channels
void list_channels(){
    struct request_list req;
    req.req_type = REQ_LIST;
    send_req(&req, sizeof(req));
}

//switch channels (used in main, join_channel, leave_channel)
void switch_channels(char *channel){
    // error handling: check if channel is in channel list
    int isChannel = 0;
    for (int i = 0; i < subscr_count; i++){
        if (strncmp(subscribed_channels[i], channel, CHANNEL_MAX) == 0) {
            isChannel = 1;
        }
    }

    // if user is not in channel, abort
    if (!isChannel){
        printf("Must switch to a subscribed channel.\n");
        return;
    }

    strncpy(active_channel, channel, CHANNEL_MAX);
    printf("Switched to channel: %s\n", channel);
}

// join channel request send
void join_channel(char *channel){
    struct request_join req;
    req.req_type = REQ_JOIN;
    strncpy(req.req_channel, channel, CHANNEL_MAX);
    send_req(&req, sizeof(req));

    // subscribe to channel
    if (subscr_count < MAX_NUM_CHANNELS) {
        strncpy(subscribed_channels[subscr_count], channel, CHANNEL_MAX);
        subscr_count++;
        strncpy(active_channel, channel, CHANNEL_MAX); // set joined channel as active
    }
}

// leave channel 
void leave_channel(char *channel) {
    struct request_leave req;
    req.req_type = REQ_LEAVE;
    strncpy(req.req_channel, channel, CHANNEL_MAX);

    send_req(&req, sizeof(req));

    // error handling: check if channel is in channel list
    int isChannel = 0;
    for (int i = 0; i < subscr_count; i++){
        if (strncmp(subscribed_channels[i], channel, CHANNEL_MAX) == 0) {
            isChannel = 1;
        }
    }

    // if user is not in channel, abort
    if (!isChannel){
        printf("Must leave a subscribed channel.\n");
        return;
    }

    // remove the channel from the channel list
    for (int i = 0; i < subscr_count; i++) {
        if (strncmp(subscribed_channels[i], channel, CHANNEL_MAX) == 0) {
            // shift needed
            for (int j = i; j < subscr_count - 1; j++) {
                strncpy(subscribed_channels[j], subscribed_channels[j + 1], CHANNEL_MAX);
            }
            subscr_count--;
            break;
        }
    }
    // clear active channel and set active channel 
    if (strncmp(active_channel, channel, CHANNEL_MAX) == 0) {
        if (subscr_count > 0){ // set active channel to last subscribed channel
            strncpy(active_channel, subscribed_channels[subscr_count], CHANNEL_MAX);
        }else{ // no channels left, active channel is unset
            memset(active_channel, 0, CHANNEL_MAX);
        }
    }
}

void say(char *message){
    if (strlen(active_channel) == 0){
                printf("You must be in a channel to send a message.\n");
                return;
    }

    struct request_say req;
    req.req_type = REQ_SAY;
    strncpy(req.req_channel, active_channel, CHANNEL_MAX); // use active channel
    strncpy(req.req_text, message, SAY_MAX);

    send_req(&req, sizeof(req));
}

void who(char *channel){
    struct request_who req;
    req.req_type = REQ_WHO;
    strncpy(req.req_channel, channel, CHANNEL_MAX);
    
    send_req(&req, sizeof(req));
}
void display(const char *channel, const char *username, const char *message){
    printf("\r\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b"); // clear prev prompt/input
    printf("[%s][%s]: %s\n", channel, username, message);
    printf("> %s", user_input); // display user input
}
// receive here, to be done in a separate thread
void *receive(void *arg) {
    char buffer[BUFFER_SIZE];
    socklen_t addr_len = sizeof(server_addr);

    while (1) {
        ssize_t recv_len = recvfrom(sockfd, buffer, BUFFER_SIZE, 0, (struct sockaddr *)&server_addr, &addr_len);
        if (recv_len == -1) {
            perror("recvfrom");
            exit(1);
        }

        // process server response
        struct text *txt = (struct text *)buffer;
        switch (txt->txt_type) {
            case TXT_SAY: {
                struct text_say *txt_say = (struct text_say *)buffer;
                display(txt_say->txt_channel, txt_say->txt_username, txt_say->txt_text);
                break;
            }
            case TXT_LIST: {
                struct text_list *txt_list = (struct text_list *)buffer;
                printf("\r\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b"); // clear
                printf("Available channels:\n");
                for (int i = 0; i < txt_list->txt_nchannels; i++) {
                    printf("- %s\n", txt_list->txt_channels[i].ch_channel);
                }
                printf("> %s", user_input); // redisplay user input
                break;
            }
            case TXT_WHO: {
                struct text_who *txt_who = (struct text_who *)buffer;
                printf("\r\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b"); // clear
                printf("Users in channel %s:\n", txt_who->txt_channel);
                for (int i = 0; i < txt_who->txt_nusernames; i++) {
                    printf("- %s\n", txt_who->txt_users[i].us_username);
                }
                printf("> %s", user_input); // redisplay user input
                break;
            }
            case TXT_ERROR: {
                struct text_error *txt_error = (struct text_error *)buffer;
                printf("\r\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b"); // clear
                printf("Error: %s\n", txt_error->txt_error);
                printf("> %s", user_input); // redisplay
                break;
            }
            default:
                printf("Received unknown message type.\n");
                break;
        }
    }
}
int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <server host> <port> <username>\n", argv[0]);
        exit(1);
    }

    char *server_host = argv[1];
    int port = atoi(argv[2]);
    char *user = argv[3];

    char buffer[BUFFER_SIZE];
    
    // create UDP socket
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Failed while creating socket. That socks...");
        exit(EXIT_FAILURE);
    }

    // set server addr
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    // server info struct
    struct server_info s_in;
    s_in.server_addr = server_addr;
    s_in.addr_len = sizeof(server_addr);

    if (inet_pton(server_addr.sin_family, server_host, &server_addr.sin_addr) <= 0) {
        perror("inet_pton");
        exit(1);
    }
    // login before they can enter commands
    login(user);

    // join Common
    join_channel("Common");

    //start receive thread here (should change before login? yo no se)
    pthread_t recv_thread;
    if (pthread_create(&recv_thread, NULL, receive, NULL) != 0) {
        perror("pthread_create");
        exit(1);
    }


    // start loop to accept user input
    while (1){
        printf("> ");
        char input[BUFFER_SIZE];
        fgets(input, BUFFER_SIZE, stdin);
        input[strcspn(input, "\n")] = 0; //remove trailing newline. credit: https://stackoverflow.com/questions/2693776/removing-trailing-newline-character-from-fgets-input
        char* tok = strtok(input, " "); //tokenize input for parsing
        
        if (strcmp(tok[0],"/exit") == 0){
            logout();
        }
        else if (strcmp(tok[0],"/list") == 0){
            list_channels();
        }
        else if (strcmp(tok[0],"/join") == 0){
            char *channel = strtok(NULL, " ");
            if (channel != NULL){
                join_channel(channel);
            }else{
                printf("Usage: /join <channel name>\n");
            }
        }
        if (strcmp(tok[0],"/leave") == 0){
            char *channel = strtok(NULL, " ");
            if (channel != NULL){
                leave_channel(channel);
            }else{
                printf("Usage: /leave <channel name>\n");
            }
        }
        else if (strcmp(tok[0],"/who") == 0){
            char *channel = strtok(NULL, " ");
            if (channel != NULL){
                who(channel);
            }else{
                printf("Usage: /who <channel name>\n");
            }
        }
        else if (strcmp(tok[0],"/switch") == 0){
            char *channel = strtok(NULL, " ");
            switch_channels(channel);
        }
        else{
            say(input);
    }
    close(sockfd);
    return 0;
}

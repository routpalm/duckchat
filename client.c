/*
client.c - for CS 432 Project 2
Nicholas Anthony
10/22/2024 (same as project 1)
*/
#include "duckchat.h"
#include "raw.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <pthread.h>
#include <ctype.h>

#define BUFFER_SIZE 1024
#define MAX_NUM_CHANNELS 100 //setting max number of subscribed channels

// globals
int sockfd;
struct sockaddr_in server_addr;
char active_channel[CHANNEL_MAX];
char subscribed_channels[MAX_NUM_CHANNELS][CHANNEL_MAX];
int subscr_count = 0; // subscribed channel count
char user_input[BUFFER_SIZE]; // save user input to display later
char username[USERNAME_MAX];

// functions
char *trim(char *str);
void send_req(void *req, size_t req_size);
void *receive();
int login(char *username);
void logout(pthread_t recv_thread);
void list_channels();
void switch_channels(char* channel);
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
int login(char *user){
    if (strlen(user) > USERNAME_MAX){
        printf("username exceeds size limit. please shorten.\n");
        return 1;
    }

    struct request_login req;
    req.req_type = REQ_LOGIN;

    strncpy(req.req_username, user, USERNAME_MAX);
    send_req(&req, sizeof(req));
    return 0;
}

//logout and quit
void logout(pthread_t recv_thread){
    struct request_logout req;
    req.req_type = REQ_LOGOUT;
    send_req(&req, sizeof(req));

    // wait for thread to finish
    pthread_cancel(recv_thread);
    pthread_join(recv_thread, NULL);

    close(sockfd);
    exit(EXIT_SUCCESS);
}

// list all current active channels
void list_channels(){
    struct request_list req;
    req.req_type = REQ_LIST;
    send_req(&req, sizeof(req));
}

void switch_channels(char *channel) {
    int isChannel = 0;
    for (int i = 0; i < subscr_count; i++) {
        if (strncmp(subscribed_channels[i], channel, CHANNEL_MAX) == 0) {
            isChannel = 1;
            break;
        }
    }

    if (!isChannel) {
        printf("You must switch to a subscribed channel.\n");
        return;
    }

    strncpy(active_channel, channel, CHANNEL_MAX);
    printf("Switched to channel: %s\n", channel);
}

// join channel request send
void join_channel(char *channel) {
    if (strlen(channel) > CHANNEL_MAX) {
        printf("channel size exceeds max limit.\n");
        return;
    }

    // Check if already subscribed
    for (int i = 0; i < subscr_count; i++) {
        if (strncmp(subscribed_channels[i], channel, CHANNEL_MAX) == 0) {
            printf("Already subscribed to channel: %s\n", channel);
            return;
        }
    }

    struct request_join req;
    req.req_type = REQ_JOIN;
    strncpy(req.req_channel, channel, CHANNEL_MAX);
    send_req(&req, sizeof(req));

    // Subscribe to channel
    if (subscr_count < MAX_NUM_CHANNELS) {
        strncpy(subscribed_channels[subscr_count], channel, CHANNEL_MAX);
        subscr_count++;
        strncpy(active_channel, channel, CHANNEL_MAX); // Set joined channel as active
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

    // empty out active channel (client must manually /switch to desired channel)
    memset(active_channel, 0, CHANNEL_MAX);
}

void say(char *message) {
    //printf("[Client] sending say message\n");
    if (strlen(active_channel) == 0) {
        printf("You must be in a channel to send a message.\n");
        return;
    }

    if (strlen(message) > SAY_MAX) {
        printf("Message exceeds size limit.\n");
        return;
    }

    struct request_say req;
    req.req_type = REQ_SAY;
    strncpy(req.req_channel, active_channel, CHANNEL_MAX); 
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
    printf("\r");  // reset cursor
    printf("[%s][%s]: %s\n", channel, username, message);
    printf("> %s", user_input);  // redisplay user input
    fflush(stdout);
}

// receive here, to be done in a separate thread
void *receive() {
    char buffer[BUFFER_SIZE];
    socklen_t addr_len = sizeof(server_addr);

    while (1) {
        //printf("Receiving on sockfd = %d\n", sockfd);
        // check if thread is canceled
        pthread_testcancel();

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
                //printf("[Client] received TXT_SAY from server\n");
                display(txt_say->txt_channel, txt_say->txt_username, txt_say->txt_text);
                break;
            }
            case TXT_LIST: {
                struct text_list *txt_list = (struct text_list *)buffer;
                printf("\r\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b"); // clear
                printf("Existing channels:\n");
                for (int i = 0; i < txt_list->txt_nchannels; i++) {
                    printf("\t%s\n", txt_list->txt_channels[i].ch_channel);
                }
                printf("> %s", user_input); // redisplay user input
                fflush(stdout);
                break;
            }
            case TXT_WHO: {
                struct text_who *txt_who = (struct text_who *)buffer;

                printf("\r\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b"); // clear
                printf("users on channel %s:\n", txt_who->txt_channel);

                for (int i = 0; i < txt_who->txt_nusernames; i++) {
                    printf("\t%s\n", txt_who->txt_users[i].us_username);
                }
                printf("> %s", user_input); // redisplay user input
                fflush(stdout);
                break;
            }
            case TXT_ERROR: {
                struct text_error *txt_error = (struct text_error *)buffer;
                printf("\r\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b"); // clear
                printf("error: %s\n", txt_error->txt_error);
                printf("> %s", user_input); // redisplay
                fflush(stdout);
                break;
            }
            default:
                printf("received unknown message type.\n");
                break;
        }
    }
}
// credit: https://stackoverflow.com/questions/656542/trim-a-string-in-c
char *trim(char *str) {
    char *ptr;
    if (!str)
        return NULL;   // handle NULL string
    if (!*str)
        return str;      // handle empty string
    for (ptr = str + strlen(str) - 1; (ptr >= str) && isspace(*ptr); --ptr);
    ptr[1] = '\0';
    return str;
}

int main(int argc, char *argv[]) {
    /*
    if (raw_mode() == -1){
        perror("raw mode");
        exit(1);
    }

    atexit(cooked_mode);
    */
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <server host> <port> <username>\n", argv[0]);
        exit(1);
    }

    char *server_host = argv[1];
    int port = atoi(argv[2]);
    char *user = argv[3];
    strncpy(username, user, USERNAME_MAX);

    
    // create UDP socket
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Failed while creating socket. That socks...");
        exit(EXIT_FAILURE);
    }
    //printf("socket created successfully (sockfd = %d).\n", sockfd);

    // set server addr
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    if (inet_pton(server_addr.sin_family, server_host, &server_addr.sin_addr) != 1) {
        perror("inet_pton failed");
        exit(1);
    }

    pthread_t recv_thread;
    if (pthread_create(&recv_thread, NULL, receive, NULL) != 0) {
        perror("pthread_create");
        exit(1);
    }

    // login before they can enter commands
    if (login(user) != 0){
        printf("exiting...\n");
        exit(1);
    }

    // join Common
    join_channel("Common");

    // user input loop
    while (1){
        printf("> ");
        fflush(stdout);

        char raw_input[BUFFER_SIZE];
        char *trimmed_input;

        fgets(raw_input, BUFFER_SIZE, stdin);
        trimmed_input = trim(raw_input);

        // if string is empty, continue since it's not sayable input
        if (strlen(trimmed_input) == 0){
            continue;
        }

        // saving input before it gets sliced up by strtok
        char saved_message[BUFFER_SIZE];
        strncpy(saved_message, trimmed_input, BUFFER_SIZE);

        char* tok = strtok(trimmed_input, " "); //tokenize input for parsing
        
        if (strcmp(tok,"/exit") == 0){
            logout(recv_thread);
        }
        else if (strcmp(tok,"/list") == 0){
            list_channels();
        }
        else if (strcmp(tok,"/join") == 0){
            char *channel = strtok(NULL, " ");
            if (channel != NULL){
                if (strlen(channel) > CHANNEL_MAX){
                    printf("channel name exceeds size limit.\n");
                }else{
                    join_channel(channel);
                }

            }else{
                printf("Usage: /join <channel name>\n");
            }
        }
        if (strcmp(tok,"/leave") == 0){
            char *channel = strtok(NULL, " ");
            if (channel != NULL){
                if (strlen(channel) > CHANNEL_MAX){
                    printf("channel name exceeds size limit.\n");
                }else{
                    leave_channel(channel);
                }
            }else{
                printf("Usage: /leave <channel name>\n");
            }
        }
        else if (strcmp(tok,"/who") == 0){
            char *channel = strtok(NULL, " ");
            if (channel != NULL){
                if (strlen(channel) > CHANNEL_MAX){
                    printf("channel name exceeds size limit.\n");
                }else{
                    who(channel);
                }
            }else{
                printf("Usage: /who <channel name>\n");
            }
        }
        else if (strcmp(tok,"/switch") == 0){
            char *channel = strtok(NULL, " ");
            if (channel != NULL){
                if (strlen(channel) > CHANNEL_MAX){
                        printf("channel name exceeds size limit.\n");
                }else{
                        switch_channels(channel);
                }
            }else{
                printf("Usage: /switch <channel name>\n");
            }
        }
        else if (tok[0] != '/'){
            //printf("[Client] (Main) Saying %s...\n", saved_message);
            if (strlen(saved_message) > SAY_MAX){
                printf("message length exceeds size limit.\n");
            }else{
                say(saved_message); 
            }
        }
    }
    return 0;
}

/*
server.c - for CS 432 Project 1
Nicholas Anthony
10/22/2024
*/
#include "duckchat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>

#define MAX_USERS 100
#define MAX_CHANNELS 100

// structs
struct user {
    char username[USERNAME_MAX];
    struct sockaddr_in addr;
};

struct channel {
    char name[CHANNEL_MAX];
    int user_count;
    struct user *users[MAX_USERS];
};

// globals
int sockfd;
struct sockaddr_in server_addr;
struct channel channels[MAX_CHANNELS];
int channel_count = 0;
struct user *users[MAX_USERS];
int user_count = 0;

// functions

struct user* find_user(struct sockaddr_in *client_addr);
struct channel* find_channel(char *channel_name);

void send_d(void *txt, size_t txt_size, struct sockaddr_in *addr);
void login(char *username, struct sockaddr_in *client_addr);
void logout(struct sockaddr_in *client_addr);
void join_channel(char *channel_name, struct sockaddr_in *client_addr);
void leave_channel(char *channel_name, struct sockaddr_in *client_addr);
void say(char *channel_name, char *message, struct sockaddr_in *client_addr);
void delete_channel(struct channel *ch);
void list_channels(struct sockaddr_in *client_addr);
void who(char *channel_name, struct sockaddr_in *client_addr);
int user_present(struct user *u, struct channel *ch);
void remove_user(char *username, struct channel *ch);
void broadcast(struct text_say *txt_say, struct channel *ch);
int validate_str(const char *str, size_t max_len);
int validate_pac(int rcv_len, int correct_len);
void send_err(char *err, struct sockaddr_in *client_addr);
/*
 * BEGIN FUNCTION DEFINITIONS
 */


/*
    send a message to a user
*/
void send_d(void *txt, size_t txt_size, struct sockaddr_in *client_addr) {
    int err = sendto(sockfd, txt, txt_size, 0, (struct sockaddr *)client_addr, sizeof(struct sockaddr_in));
    if (err < 0) {
        perror("send");
        exit(1);
    }
}

/*
    login a user and add to user list
*/
void login(char *username, struct sockaddr_in *client_addr) {
    
    // user must have a unique name, check if user already exists
    struct user *existing_user = find_user(client_addr);
    if (existing_user != NULL) {
        printf("user %s already logged in.\n", existing_user->username);
        return;
    }

    // create new user
    struct user *new_user = (struct user *)malloc(sizeof(struct user));
    if (new_user == NULL) {
        perror("malloc");
    return;
}
    //force null termination
    strncpy(new_user->username, username, USERNAME_MAX - 1);
    new_user->username[USERNAME_MAX - 1] = '\0';
    new_user->addr = *client_addr;
    users[user_count++] = new_user;

    printf("user %s logged in.\n", username);
}

/*
    logout user and remove from user list/channels
*/
void logout(struct sockaddr_in *client_addr) {

    // find user
    struct user *u = find_user(client_addr);

    if (u == NULL) {
        printf("unknown user trying to logout.\n");
        return;
    }

    char username[USERNAME_MAX];
    strcpy(username, u->username);
    
    // remove from all channels (we don't discriminate)
    for (int i = 0; i < channel_count; i++) {
        leave_channel(channels[i].name,client_addr);
    }

    // remove from user list
    for (int i = 0; i < user_count; i++) {
        if (strcmp(users[i]->username, username) == 0) {
            free(users[i]);
            users[i] = users[--user_count]; 
            users[user_count] = NULL; 
            break;
        }
    }

    printf("user %s logged out.\n", username);
}

/*
    add user to channel/create channel
*/
void join_channel(char *channel_name, struct sockaddr_in *client_addr) {
    
    // find user
    struct user *u = find_user(client_addr);
    if (u == NULL) {
        printf("user not found for join request.\n");
        return;
    }

    struct channel *ch = find_channel(channel_name);
    if (ch == NULL) { // if channel doesn't exist, create it
        struct channel new_channel;
        strncpy(new_channel.name, channel_name, CHANNEL_MAX - 1); // safe copy with null termination
        new_channel.name[CHANNEL_MAX - 1] = '\0';
        new_channel.user_count = 0;
        channels[channel_count++] = new_channel;
        ch = &channels[channel_count - 1];

        printf("new channel %s created.\n", channel_name);
    }

    if (!user_present(u, ch)) { // user cannot join channel that they already are subscribed to
        ch->users[ch->user_count++] = u;
        printf("user %s joined channel %s.\n", u->username, ch->name);
    } else {
        printf("user %s already in channel %s.\n", u->username, ch->name);
        send_err("You have already joined this channel.", client_addr);
    }
}

/*
    remove user from channel
*/
void leave_channel(char *channel_name, struct sockaddr_in *client_addr) {
    struct user *u = find_user(client_addr);
    if (u == NULL) {
        printf("user not found for leave request.\n");
        return;
    }

    struct channel *ch = find_channel(channel_name);
    if (ch == NULL) {
        printf("channel %s not found.\n", channel_name);
        return;
    }

    if (user_present(u, ch)) {
        remove_user(u->username, ch);
        printf("user %s left channel %s.\n", u->username, ch->name);
    } else {
        printf("user %s not in channel %s.\n", u->username, ch->name);
    }
}

/*
    server-side handling of say request
*/
void say(char *channel_name, char *message, struct sockaddr_in *client_addr) {
    struct user *u = find_user(client_addr);
    if (u == NULL) {
        printf("user not found for say request.\n");
        return;
    }

    struct channel *ch = find_channel(channel_name);
    if (ch == NULL) {
        printf("channel %s not found.\n", channel_name);
        return;
    }

    if (!user_present(u, ch)) {
        printf("user %s is not in channel %s.\n", u->username, ch->name);
        return;
    }

    // construct say struct and broadcast to all users in the channel
    struct text_say txt_say;
    txt_say.txt_type = TXT_SAY;
    strncpy(txt_say.txt_channel, channel_name, CHANNEL_MAX);
    strncpy(txt_say.txt_username, u->username, USERNAME_MAX);
    strncpy(txt_say.txt_text, message, SAY_MAX);

    broadcast(&txt_say, ch);
}

/*
    list all channels that exist
*/
void list_channels(struct sockaddr_in *client_addr) {
    struct text_list txt_list;
    txt_list.txt_type = TXT_LIST;
    txt_list.txt_nchannels = channel_count;

    // prepare to send list of channels
    int size = sizeof(struct text_list) + channel_count * sizeof(struct channel_info);
    struct text_list *txt = (struct text_list *)malloc(size);
    txt->txt_type = TXT_LIST;
    txt->txt_nchannels = channel_count;

    for (int i = 0; i < channel_count; i++) {
        strncpy(txt->txt_channels[i].ch_channel, channels[i].name, CHANNEL_MAX);
    }

    send_d(txt, size, client_addr);
    free(txt);
}

/*
    server-side implementation of who (send list of users in specified channel)
*/
void who(char *channel_name, struct sockaddr_in *client_addr) {
    struct channel *ch = find_channel(channel_name);
    
    // check if channel exists
    if (ch == NULL) {
        printf("channel %s not found.\n", channel_name);
        return;
    }

    // create who struct
    struct text_who txt_who;
    txt_who.txt_type = TXT_WHO;
    txt_who.txt_nusernames = ch->user_count;
    strncpy(txt_who.txt_channel, channel_name, CHANNEL_MAX);

    // determine size of list and fill in params
    int size = sizeof(struct text_who) + ch->user_count * sizeof(struct user_info);
    struct text_who *txt = (struct text_who *)malloc(size);
    txt->txt_type = TXT_WHO;
    txt->txt_nusernames = ch->user_count;
    strncpy(txt->txt_channel, channel_name, CHANNEL_MAX);

    // iterate through struct array and add usernames
    for (int i = 0; i < ch->user_count; i++) {
        strncpy(txt->txt_users[i].us_username, ch->users[i]->username, USERNAME_MAX);
    }

    send_d(txt, size, client_addr);
    free(txt);
}

/*
    find user according to address & port
*/
struct user* find_user(struct sockaddr_in *client_addr) {
    for (int i = 0; i < user_count; i++) {
        if (memcmp(&users[i]->addr.sin_addr, &client_addr->sin_addr, sizeof(struct in_addr)) == 0 &&
            users[i]->addr.sin_port == client_addr->sin_port) {
            return users[i];
        }
    }
    return NULL;
}
/*
    find specified channel in channel list
*/
struct channel* find_channel(char *channel_name) {
    for (int i = 0; i < channel_count; i++) {
        if (strcmp(channels[i].name, channel_name) == 0) {
            return &channels[i];
        }
    }
    return NULL;
}

/*
    check if user present in channel
*/
int user_present(struct user *u, struct channel *ch) {
    for (int i = 0; i < ch->user_count; i++) {
        if (strcmp(ch->users[i]->username, u->username) == 0) {
            return 1; // user present
        }
    }
    return 0;
}

/* 
    remove user from channel/delete channel if user count is 0
*/
void remove_user(char *username, struct channel *ch) {
    for (int i = 0; i < ch->user_count; i++) {
        if (strcmp(ch->users[i]->username, username) == 0) {
            // move last user to current position and decrement user count
            ch->users[i] = ch->users[ch->user_count - 1];
            ch->users[ch->user_count - 1] = NULL;
            ch->user_count--;

            // if no users, delete channel (except Common)
            if (ch->user_count == 0 && strncmp(ch->name, "Common", CHANNEL_MAX) != 0) {
                delete_channel(ch);
            }
            return;
        }
    }
}

/*
    delete channel and decrement chanel count
*/
void delete_channel(struct channel *ch){
    printf("deleting channel %s\n", ch->name);
    for (int i = 0; i < channel_count; i++){
        if (&channels[i] == ch){
            for (int j = i; j < channel_count - 1; j++) {
                channels[j] = channels[j + 1];
            }

            channel_count--;
            printf("channel %s deleted.\n", ch->name);
            break;
        }
    }

}

/*
    broadcast message to all users in channel
*/
void broadcast(struct text_say *txt_say, struct channel *ch) {
    for (int i = 0; i < ch->user_count; i++) {
        send_d(txt_say, sizeof(struct text_say), &ch->users[i]->addr);
    }
}

/*
    checks input text for size restrictions or a lack of a null terminator
*/
int validate_str(const char *str, size_t max_len){
    if (strnlen(str, max_len) >= max_len) {
        return 0; // too long or not null-terminated
    }
    return 1; // valid
}
/*
    checks size of incoming packet to make sure that it's not too short
*/
int validate_pac(int rcv_len, int correct_len){
    if (rcv_len < correct_len){
        printf("malformed packet detected and dropped.\n");
        return 0;
    }
    return 1; // valid
}

void send_err(char *err, struct sockaddr_in *client_addr){
    struct user *u = find_user(client_addr);
    if (u == NULL) {
        printf("user not found for err request.\n");
        return;
    }
    int size = sizeof(struct text_error) + strlen(err);
    struct text_error *txt_err = (struct text_error *)malloc(size);
    txt_err->txt_type = TXT_ERROR;
    strncpy(txt_err->txt_error, err, SAY_MAX);

    send_d(txt_err, sizeof(struct text_error), client_addr);
    free(txt_err);
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        printf("Usage: %s <server IP> <port>\n", argv[0]);
        exit(1);
    }

    char *server_ip = argv[1];
    int port = atoi(argv[2]);

    // setup UDP sock
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket");
        exit(1);
    }

    // setup addr
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    inet_pton(AF_INET, server_ip, &server_addr.sin_addr);

    // binding socket to addr
    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        exit(1);
    }

    printf("DuckChat is listening on ip:port: %s:%d...\n", server_ip, port);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        char buffer[1024];
        int len = recvfrom(sockfd, buffer, sizeof(buffer), 0, (struct sockaddr *)&client_addr, &addr_len);
        if (len < 0) {
            perror("recvfrom");
            continue;
        }
        
        struct request *req = (struct request *)buffer;
    
        switch (req->req_type) {
            case REQ_LOGIN: {
                if (!validate_pac(len, sizeof(struct request_login))) {
                    send_err("LOGIN: packet length too long", &client_addr);
                    break; // validate length of packet
                }
                struct request_login *req_login = (struct request_login *)buffer;
                if (!validate_str(req_login->req_username, USERNAME_MAX)){
                    send_err("LOGIN: username length too long", &client_addr);
                    break; // validate length of user
                }
                login(req_login->req_username, &client_addr);
                break;
            }
            case REQ_LOGOUT: {
                if (!validate_pac(len, sizeof(struct request_logout))) {
                    send_err("LOGOUT: packet length too long", &client_addr);
                    break; // validate length of packet
                }
                logout(&client_addr);
                break;
            }
            case REQ_JOIN: {
                if (!validate_pac(len, sizeof(struct request_join))) {
                    send_err("JOIN: packet length too long", &client_addr);
                    break; // validate length of packet
                }
                struct request_join *req_join = (struct request_join *)buffer;
                if (!validate_str(req_join->req_channel, USERNAME_MAX)){
                    send_err("JOIN: channel length too long", &client_addr);
                    break; // validate length of channel
                }
                join_channel(req_join->req_channel, &client_addr);
                break;
            }
            case REQ_LEAVE: {
                if (!validate_pac(len, sizeof(struct request_leave))) {
                    send_err("LEAVE: packet length too long", &client_addr);
                    break; // validate length of packet
                }
                struct request_leave *req_leave = (struct request_leave *)buffer;
                if (!validate_str(req_leave->req_channel, USERNAME_MAX)){
                    send_err("JOIN: channel length too long", &client_addr);
                    break; // validate length of channel
                }
                leave_channel(req_leave->req_channel, &client_addr);
                break;
            }
            case REQ_SAY: {
                if (!validate_pac(len, sizeof(struct request_say))) {
                    send_err("SAY: packet length too long", &client_addr);
                    break; // validate length of packet
                }
                struct request_say *req_say = (struct request_say *)buffer;
                if (!validate_str(req_say->req_text, SAY_MAX)){
                    send_err("SAY: message length too long", &client_addr);
                    break; // validate length of message
                }
                say(req_say->req_channel, req_say->req_text, &client_addr);
                break;
            }
            case REQ_LIST: {
                if (!validate_pac(len, sizeof(struct request_list))) {
                    send_err("LIST: packet length too long\n", &client_addr);
                    break; // validate length of packet
                }
                list_channels(&client_addr);
                break;
            }
            case REQ_WHO: {
                if (!validate_pac(len, sizeof(struct request_who))) {
                    send_err("WHO: packet length too long", &client_addr);
                    break; // validate length of packet
                }
                struct request_who *req_who = (struct request_who *)buffer;
                if (!validate_str(req_who->req_channel, CHANNEL_MAX)){
                    send_err("WHO: channel length too long", &client_addr);
                    break; // validate length of message
                }
                who(req_who->req_channel, &client_addr);
                break;
            }
            default: {
                send_err("request type unknown.",&client_addr);
                break;
            }
        }
    }
    close(sockfd);
    return 0;
}

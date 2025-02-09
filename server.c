/*
server.c - for CS 432 Project 2
Nicholas Anthony
11/30/2024
*/
#include "duckchat.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/time.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>
#include <stdarg.h>

#define MAX_USERS 100
#define MAX_CHANNELS 100
#define MAX_MESSAGE_IDS 100

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

struct neighbor {
    struct sockaddr_in addr;
    int active;
    time_t last_active; // timestamp last seen active
};


struct routing_table {
    char channel_name[CHANNEL_MAX];
    struct neighbor *subscribed_neighbors[MAX_CHANNELS];
    int neighbor_count;
};

struct message_id {
    uint64_t id;
    time_t timestamp;
};

// global struct vars
struct sockaddr_in server_addr;
struct channel channels[MAX_CHANNELS];
struct user *users[MAX_USERS];
struct neighbor neighbors[MAX_CHANNELS];
struct routing_table routing_table[MAX_CHANNELS];
struct message_id rcnt_message_ids[MAX_MESSAGE_IDS];

// global int/count vars
int sockfd;
int channel_count = 0;
int user_count = 0;
int neighbor_count = 0;
int routing_table_count = 0;
int message_count = 0;
time_t start_time = 0;


// functions
struct user* find_user(struct sockaddr_in *client_addr);
struct channel* find_channel(char *channel_name);
struct routing_table *find_rt_entry(char *channel_name);
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
void init_neighbors(int argc, char *argv[]);
void s2s_join(char *channel_name);
void s2s_leave(char *channel_name);
void s2s_say(char *username, char *channel_name, char *message, uint64_t unique_id);
void renew_join();
void add_neighbor_to_channel(char *channel_name, struct sockaddr_in *neighbor_addr);
void remove_neighbor_from_channel(char *channel_name, struct sockaddr_in *neighbor_addr);
int isdup(uint64_t message_id);
void prune();
void log_message(const struct sockaddr_in *local_addr, const struct sockaddr_in *remote_addr,
                 const char *direction, const char *message_type, const char *channel,
                const char *username, const char *text);
void fwd_s2s_join(char *channel_name, struct sockaddr_in *sender_addr);
void *timer_thread(void *arg);
void init_random();
void server_print(const char *fmt, ...);
uint64_t generate_unique_id();
void delete_rt_entry(char *channel_name);
/*
 * BEGIN FUNCTION DEFINITIONS
 */
/*
    used to delete internal records (as per the guide) of channel after sending a leave
*/
void delete_rt_entry(char *channel_name) {
    for (int i = 0; i < routing_table_count; i++) {
        if (strncmp(routing_table[i].channel_name, channel_name, CHANNEL_MAX) == 0) {
            // shift up
            for (int j = i; j < routing_table_count - 1; j++) {
                routing_table[j] = routing_table[j + 1];
            }
            routing_table_count--;
            server_print("Deleted routing table entry for channel %s.\n", channel_name);
            break;
        }
    }
}
/*
 from https://medium.com/@turman1701/va-list-in-c-exploring-ft-printf-bb2a19fcd128
*/
void server_print(const char *fmt, ...) {
    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &server_addr.sin_addr, ip_str, INET_ADDRSTRLEN);
    int port = ntohs(server_addr.sin_port);

    printf("%s:%d ", ip_str, port);

    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}
/*
    create unique ID from seed
*/
uint64_t generate_unique_id() {
    uint64_t id = ((uint64_t)rand() << 32) | rand();
    return id;
}
/*
    create seed from urandom
*/
void init_random() {
    FILE *fp = fopen("/dev/urandom", "rb");
    unsigned int seed;
    if (fp == NULL) {
        perror("Error opening /dev/urandom");
        exit(1);
    }
    if (fread(&seed, sizeof(seed), 1, fp) != 1) {
        perror("Error reading from /dev/urandom");
        fclose(fp);
        exit(1);
    }
    fclose(fp);
    srand(seed);
}

void *timer_thread(void *arg) {
    (void)arg;
    while (1) {
        sleep(1);

        time_t now = time(NULL);

        // check if its time to renew joins
        static time_t last_renew = 0;
        if (now - last_renew >= 60) {
            renew_join();
            last_renew = now;
        }

        // prune inactive neighbors
        prune();
    }
    return NULL;
}
/*
    logging function for the s2s messages
*/
void log_message(const struct sockaddr_in *local_addr, const struct sockaddr_in *remote_addr,
                 const char *direction, const char *message_type, const char *channel,
                 const char *username, const char *text) {

    char local_ip[INET_ADDRSTRLEN];
    char remote_ip[INET_ADDRSTRLEN];

    inet_ntop(AF_INET, &local_addr->sin_addr, local_ip, INET_ADDRSTRLEN);
    inet_ntop(AF_INET, &remote_addr->sin_addr, remote_ip, INET_ADDRSTRLEN);

    printf("%s:%d %s:%d %s %s %s",
           local_ip, ntohs(local_addr->sin_port),
           remote_ip, ntohs(remote_addr->sin_port),
           direction, message_type, channel);

    if (username != NULL) {
        printf(" %s", username);
    }

    if (text != NULL) {
        printf(" \"%s\"", text);
    }

    printf("\n");
}

/*
    finds a routing table entry based on a channel name
*/
struct routing_table *find_rt_entry(char *channel_name) {
    for (int i = 0; i < routing_table_count; i++) {
        if (strncmp(routing_table[i].channel_name, channel_name, CHANNEL_MAX) == 0) {
            return &routing_table[i];
        }
    }
    return NULL;
}
/*
    checks if a given message id is a duplicate or new, and adds it to rcnt_message_ids for loop detection
*/
int isdup(uint64_t message_id) {
    time_t now = time(NULL);


    for (int i = 0; i < message_count; i++) {
        if (rcnt_message_ids[i].id == message_id) {
            return 1; // dup found
        }
    }

    // add new message ID if it exists
    if (message_count < MAX_MESSAGE_IDS) {
        rcnt_message_ids[message_count].id = message_id;
        rcnt_message_ids[message_count].timestamp = now;
        message_count++;
    } else {
        // overwrite oldest entry
        int oldest_index = 0;
        for (int i = 1; i < MAX_MESSAGE_IDS; i++) {
            if (rcnt_message_ids[i].timestamp < rcnt_message_ids[oldest_index].timestamp) {
                oldest_index = i;
            }
        }
        rcnt_message_ids[oldest_index].id = message_id;
        rcnt_message_ids[oldest_index].timestamp = now;
    }
    return 0; // nodup
}

/*
    function used in conjunction with the timer to renew join messages to each channel
*/
void renew_join() {
    for (int i = 0; i < routing_table_count; i++) {
        struct routing_table *rt = &routing_table[i];

        // for each channel we are subscribed to, send join to neighbors
        struct s2s_join join_msg;
        join_msg.req_type = S2S_JOIN;
        strncpy(join_msg.req_channel, rt->channel_name, CHANNEL_MAX);

        for (int j = 0; j < neighbor_count; j++) {
            struct neighbor *nbr = &neighbors[j];
            send_d(&join_msg, sizeof(join_msg), &nbr->addr);

            log_message(&server_addr, &nbr->addr, "renew", "S2S Join", rt->channel_name, NULL, NULL);
        }
    }
}

void prune() {
    time_t now = time(NULL);

    // dont want to prune neighbors in first 2 minutes of runtime
    if(start_time != 0 && now - start_time < 119) {
        return;
    }
    //printf("prune() called at %ld\n", now);
    for (int i = 0; i < routing_table_count; i++) {
        struct routing_table *rt = &routing_table[i];
        int inactive_neighbors[MAX_CHANNELS];
        int inactive_count = 0;

        // mark neighbors for removal >:)
        for (int j = 0; j < rt->neighbor_count; j++) {
            struct neighbor *nbr = rt->subscribed_neighbors[j];

            // check for inactivity
            if (now - nbr->last_active > 120) {
                inactive_neighbors[inactive_count++] = j;
                log_message(&server_addr, &nbr->addr, "prune", "S2S Leave", NULL, NULL, "Neighbor inactivity exceeded 120 seconds");
            }
        }

        // remove em
        for (int k = 0; k < inactive_count; k++) {
            struct neighbor *nbr = rt->subscribed_neighbors[inactive_neighbors[k]];
            remove_neighbor_from_channel(rt->channel_name, &nbr->addr);
        }
    }
}


/*
    add "neighboring" servers to current server
*/
void init_neighbors(int argc, char *argv[]) {
    if ((argc - 3) % 2 != 0) {
        fprintf(stderr, "Error: Each neighbor requires an IP and a port\n");
        exit(1);
    }

    memset(neighbors, 0, sizeof(neighbors));

    // iterate over all args, each pair is a neighbor's IP and port (supposedly)
    for (int i = 3; i < argc; i += 2) {
        char *neighbor_ip = argv[i];
        int neighbor_port = atoi(argv[i + 1]);

        struct neighbor new_neighbor;
        memset(&new_neighbor, 0, sizeof(new_neighbor));
        new_neighbor.addr.sin_family = AF_INET;
        new_neighbor.addr.sin_port = htons(neighbor_port);

        if (inet_pton(AF_INET, neighbor_ip, &new_neighbor.addr.sin_addr) <= 0) {
            if (strcmp(neighbor_ip, "localhost") == 0){
                new_neighbor.addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            }else{
                perror("Invalid IP address");
                continue;
            }
        }
        new_neighbor.active = 1; 
        new_neighbor.last_active = time(NULL); 

        neighbors[neighbor_count++] = new_neighbor;
        server_print("added neighbor: %s:%d\n", neighbor_ip, neighbor_port);
    }
}
/*
    add a neighbor to a channel (after S2S join request)
*/
void add_neighbor_to_channel(char *channel_name, struct sockaddr_in *neighbor_addr) {
    struct routing_table *rt = find_rt_entry(channel_name);

    if (rt == NULL) {
        rt = &routing_table[routing_table_count++];
        strncpy(rt->channel_name, channel_name, CHANNEL_MAX);
        rt->neighbor_count = 0;
    }

    // check if neighbor already exists in channel's neighbor list
    for (int i = 0; i < rt->neighbor_count; i++) {
        if (rt->subscribed_neighbors[i]->addr.sin_addr.s_addr == neighbor_addr->sin_addr.s_addr &&
            rt->subscribed_neighbors[i]->addr.sin_port == neighbor_addr->sin_port) {
            rt->subscribed_neighbors[i]->last_active = time(NULL);
            rt->subscribed_neighbors[i]->active = 1;
            //printf("neighbor already exists in channel neighbor list\n");
            return;
        }
    }

    // check if neighbor already exists in neighbors[]
    struct neighbor *nbr = NULL;
    for (int i = 0; i < neighbor_count; i++) {
        if (neighbors[i].addr.sin_addr.s_addr == neighbor_addr->sin_addr.s_addr &&
            neighbors[i].addr.sin_port == neighbor_addr->sin_port) {
            nbr = &neighbors[i];
            break;
        }
    }

    // if neighbor not found in neighbors[], add it
    if (nbr == NULL) {
        nbr = &neighbors[neighbor_count++];
        nbr->addr = *neighbor_addr;
        nbr->active = 1;
        nbr->last_active = time(NULL);
    }

    // add to rt
    rt->subscribed_neighbors[rt->neighbor_count++] = nbr;
    server_print("Added neighbor %s:%d to channel %s.\n",
    inet_ntoa(neighbor_addr->sin_addr), ntohs(neighbor_addr->sin_port), channel_name);
}

/*
    remove neighbor from a channel's routing table (after leave request)
*/
void remove_neighbor_from_channel(char *channel_name, struct sockaddr_in *neighbor_addr){
    for (int i = 0; i < routing_table_count; i++) {
        // need to check if channel exists, so loop through all channels and check
        if (strncmp(routing_table[i].channel_name, channel_name, CHANNEL_MAX) == 0) {
            struct routing_table *rt = &routing_table[i];
            for (int j = 0; j < rt->neighbor_count; j++) {
                if (memcmp(&rt->subscribed_neighbors[j]->addr, neighbor_addr, sizeof(struct sockaddr_in)) == 0) {
                    // shift other neighbors down and remove neighbor from rt
                    for (int k = j; k < rt->neighbor_count - 1; k++) {
                        rt->subscribed_neighbors[k] = rt->subscribed_neighbors[k + 1];
                    }
                    rt->neighbor_count--;
                    server_print("removed neighbor %s:%d from channel %s\n", inet_ntoa(neighbor_addr->sin_addr), ntohs(neighbor_addr->sin_port), channel_name);
                    return;
                }
            }
        }
    }
}
/*
    S2S implementation of join, share all joins with neighbors
*/
void s2s_join(char *channel_name) {
    struct routing_table *rt = find_rt_entry(channel_name);

    // create new routing table entry (if it doesn't exist)
    if (rt == NULL) {
        rt = &routing_table[routing_table_count++];
        strncpy(rt->channel_name, channel_name, CHANNEL_MAX);
        rt->neighbor_count = 0;
    }

    struct s2s_join join_msg;
    join_msg.req_type = S2S_JOIN;
    strncpy(join_msg.req_channel, channel_name, CHANNEL_MAX);

    for (int i = 0; i < neighbor_count; i++) {
        struct neighbor *nbr = &neighbors[i];

        int already_neighbor = 0;
        for (int j = 0; j < rt->neighbor_count; j++) {
            if (rt->subscribed_neighbors[j] == nbr) {
                already_neighbor = 1;
                break;
            }
        }

        if (!already_neighbor) {
            rt->subscribed_neighbors[rt->neighbor_count++] = nbr;
        }

        send_d(&join_msg, sizeof(join_msg), &nbr->addr);

        log_message(&server_addr, &nbr->addr, "send", "S2S Join", channel_name, NULL, NULL);
    }
}
/*
    fairly similar to s2s join, however used to avoid redundant join messages
*/
void fwd_s2s_join(char *channel_name, struct sockaddr_in *sender_addr) {
    struct s2s_join join_msg;
    join_msg.req_type = S2S_JOIN;
    strncpy(join_msg.req_channel, channel_name, CHANNEL_MAX);

    struct routing_table *rt = find_rt_entry(channel_name);
    if (rt == NULL) {
        rt = &routing_table[routing_table_count++];
        strncpy(rt->channel_name, channel_name, CHANNEL_MAX);
        rt->neighbor_count = 0;
    }

    // loop over neighbors and send joins
    for (int i = 0; i < neighbor_count; i++) {
        struct neighbor *nbr = &neighbors[i];

        // skip sender
        if (nbr->addr.sin_addr.s_addr == sender_addr->sin_addr.s_addr &&
            nbr->addr.sin_port == sender_addr->sin_port) {
            continue;
        }

        // add neighbor to rt if not present already
        int already_neighbor = 0;
        for (int j = 0; j < rt->neighbor_count; j++) {
            if (rt->subscribed_neighbors[j] == nbr) {
                already_neighbor = 1;
                break;
            }
        }

        if (!already_neighbor) {
            rt->subscribed_neighbors[rt->neighbor_count++] = nbr;
            server_print("Added neighbor %s:%d to channel %s.\n",
                   inet_ntoa(nbr->addr.sin_addr), ntohs(nbr->addr.sin_port), channel_name);
        }

        send_d(&join_msg, sizeof(join_msg), &nbr->addr);

        log_message(&server_addr, &nbr->addr, "send", "S2S Join", channel_name, NULL, NULL);
    }
}


/*
    S2S implementation of leave, share all leaves with neighbors
*/
void s2s_leave(char *channel_name) {
    struct s2s_leave leave_msg;
    leave_msg.req_type = S2S_LEAVE;
    strncpy(leave_msg.req_channel, channel_name, CHANNEL_MAX);

    struct routing_table *rt = find_rt_entry(channel_name);
    if (rt) {
        for (int i = 0; i < rt->neighbor_count; i++) {
            struct neighbor *nbr = rt->subscribed_neighbors[i];
            send_d(&leave_msg, sizeof(leave_msg), &nbr->addr);

            log_message(&server_addr, &nbr->addr, "send", "S2S Leave", channel_name, NULL, NULL);
        }
    } else {
        server_print("no active neighbors for channel %s to send leave.\n", channel_name);
    }
}

/*
    send S2S say message with unique message id
*/
void s2s_say(char *username, char *channel_name, char *message, uint64_t unique_id) {
    struct s2s_say say_msg;
    say_msg.req_type = S2S_SAY;
    say_msg.unique_id = unique_id;
    strncpy(say_msg.req_username, username, USERNAME_MAX);
    strncpy(say_msg.req_channel, channel_name, CHANNEL_MAX);
    strncpy(say_msg.req_text, message, SAY_MAX);

    for (int i = 0; i < neighbor_count; i++) {
        send_d(&say_msg, sizeof(say_msg), &neighbors[i].addr);

        log_message(&server_addr, &neighbors[i].addr, "send", "S2S Say",channel_name, username, message);
    }
}                     
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
        server_print("user %s already logged in.\n", existing_user->username);
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

    server_print("user %s logged in.\n", username);
}

/*
    logout user and remove from user list/channels
*/
void logout(struct sockaddr_in *client_addr) {

    // find user
    struct user *u = find_user(client_addr);

    if (u == NULL) {
        server_print("unknown user trying to logout.\n");
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

    server_print("user %s logged out.\n", username);
}

/*
    add user to channel/create channel
*/
void join_channel(char *channel_name, struct sockaddr_in *client_addr) {
    
    // find user
    struct user *u = find_user(client_addr);
    if (u == NULL) {
        server_print("user not found for join request.\n");
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

        server_print("new channel %s created.\n", channel_name);
    }
    s2s_join(channel_name);
    if (!user_present(u, ch)) { // user cannot join channel that they already are subscribed to
        ch->users[ch->user_count++] = u;
        server_print("user %s joined channel %s.\n", u->username, ch->name);
    } else {
        server_print("user %s already in channel %s.\n", u->username, ch->name);
        send_err("You have already joined this channel.", client_addr);
    }
}

/*
    remove user from channel
*/
void leave_channel(char *channel_name, struct sockaddr_in *client_addr) {
    struct user *u = find_user(client_addr);
    if (u == NULL) {
        server_print("user not found for leave request.\n");
        return;
    }

    struct channel *ch = find_channel(channel_name);
    if (ch == NULL) {
        server_print("channel %s not found.\n", channel_name);
        return;
    }

    if (user_present(u, ch)) {
        remove_user(u->username, ch);
        server_print("user %s left channel %s.\n", u->username, ch->name);

        // if channel is empty, S2S leave
        if (ch->user_count == 0) {
            s2s_leave(channel_name);
        }
    } else {
        server_print("user %s not in channel %s.\n", u->username, ch->name);
    }
}

/*
    server-side handling of say request
*/
void say(char *channel_name, char *message, struct sockaddr_in *client_addr) {
    struct user *u = find_user(client_addr);
    if (u == NULL) {
        server_print("user not found for say request.\n");
        return;
    }

    struct channel *ch = find_channel(channel_name);
    if (ch == NULL) {
        server_print("channel %s not found.\n", channel_name);
        return;
    }

    if (!user_present(u, ch)) {
        server_print("user %s is not in channel %s.\n", u->username, ch->name);
        return;
    }

    // construct say struct and broadcast to all users in the channel
    struct text_say txt_say;
    txt_say.txt_type = TXT_SAY;
    strncpy(txt_say.txt_channel, channel_name, CHANNEL_MAX);
    strncpy(txt_say.txt_username, u->username, USERNAME_MAX);
    strncpy(txt_say.txt_text, message, SAY_MAX);

    server_print("%s sends say message in %s.\n", u->username, txt_say.txt_channel);
    broadcast(&txt_say, ch);

    // generate unique message ID and broadcast the S2S say to the neighbors
    uint64_t u_id = generate_unique_id();

    s2s_say(u->username, channel_name, message, u_id);
}

/*
    list all channels that exist
*/
void list_channels(struct sockaddr_in *client_addr) {

    struct user *u = find_user(client_addr);
    if (u == NULL) {
        server_print("user not found for list request.\n");
        return;
    }

    // prepare to send list of channels
    int size = sizeof(struct text_list) + channel_count * sizeof(struct channel_info);
    struct text_list *txt = (struct text_list *)malloc(size);
    txt->txt_type = TXT_LIST;
    txt->txt_nchannels = channel_count;

    for (int i = 0; i < channel_count; i++) {
        strncpy(txt->txt_channels[i].ch_channel, channels[i].name, CHANNEL_MAX);
    }

    server_print("%s requests channel list.\n", u->username);
    send_d(txt, size, client_addr);
    free(txt);
}

/*
    send list of users in specified channel back to user
*/
void who(char *channel_name, struct sockaddr_in *client_addr) {
    // check user validity
    struct user *u = find_user(client_addr);
    if (u == NULL) {
        server_print("user not found for who request.\n");
        return;
    }
    
    struct channel *ch = find_channel(channel_name);
    
    // check if channel exists
    if (ch == NULL) {
        server_print("channel %s not found.\n", channel_name);
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

    server_print("Sending who response to %s.\n", u->username);
    send_d(txt, size, client_addr);
    free(txt);
}

/*
    find user according to address & port
*/
struct user* find_user(struct sockaddr_in *client_addr) {
    for (int i = 0; i < user_count; i++) {
        // checking two params: IP address & port. since we need to differentiate between clients from the same IP
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
            return &channels[i]; // channel found and returning
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
    server_print("deleting channel %s\n", ch->name);
    for (int i = 0; i < channel_count; i++){
        if (&channels[i] == ch){
            for (int j = i; j < channel_count - 1; j++) {
                channels[j] = channels[j + 1];
            }

            channel_count--;
            server_print("channel %s deleted.\n", ch->name);
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
    check input text for size restrictions or a lack of a null terminator
*/
int validate_str(const char *str, size_t max_len){
    if (strnlen(str, max_len) >= max_len) {
        return 0; // too long or not null-terminated
    }
    return 1; // valid
}
/*
    check size of incoming packet to make sure that it's not too short
*/
int validate_pac(int rcv_len, int correct_len){
    if (rcv_len < correct_len){
        server_print("malformed packet detected and dropped.\n");
        return 0;
    }
    return 1; // valid
}

/*
    send an error to the client
*/
void send_err(char *err, struct sockaddr_in *client_addr){
    /* NOTE: don't think this is needed since there can be an error logging in and the user won't be created
    struct user *u = find_user(client_addr);
    if (u == NULL) {
        printf("user not found for err request.\n");
        return;
    }
    */
    int size = sizeof(struct text_error) + strlen(err);
    struct text_error *txt_err = (struct text_error *)malloc(size);
    txt_err->txt_type = TXT_ERROR;
    strncpy(txt_err->txt_error, err, SAY_MAX);

    send_d(txt_err, sizeof(struct text_error), client_addr);
    free(txt_err);
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Usage: %s <server IP> <port> [<neighbor IP> <neighbor port>]...\n", argv[0]);
        exit(1);
    }

    init_random();
    start_time = time(NULL);

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
    int pton_ret = inet_pton(AF_INET, server_ip, &server_addr.sin_addr);
    if (pton_ret == 0) {
        if (strcmp(server_ip, "localhost") == 0) {
            server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        } else {
            fprintf(stderr, "invalid server IP address format: %s\n", server_ip);
            exit(1);
        }
    } else if (pton_ret < 0) {
        perror("inet_pton");
        exit(1);
    }

    // bind socket to addr
    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        exit(1);
    }
    // add neighbors to global array
    init_neighbors(argc, argv);

    // setup our soft-state thread
    pthread_t timer_thread_id;
    pthread_create(&timer_thread_id, NULL, timer_thread, NULL);

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
    
        // update neighbor's last_active time
        for (int i = 0; i < neighbor_count; i++) {
            if (memcmp(&neighbors[i].addr, &client_addr, sizeof(struct sockaddr_in)) == 0) {
                neighbors[i].last_active = time(NULL);
                break;
            }
        }

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
                if (!validate_str(req_join->req_channel, CHANNEL_MAX)){
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
            case S2S_JOIN: {
                struct s2s_join *join_msg = (struct s2s_join *)buffer;
                
                log_message(&server_addr, &client_addr, "recv", "S2S Join", join_msg->req_channel, NULL, NULL);
                
                // check if already subscribed to channel
                struct routing_table *rt = find_rt_entry(join_msg->req_channel);
                int already_subscribed = (rt != NULL);

                
                add_neighbor_to_channel(join_msg->req_channel, &client_addr);

                // fwd join to other neighbors if not already subscribed
                if (!already_subscribed) {
                    fwd_s2s_join(join_msg->req_channel, &client_addr);
                }
                break;
            }
            case S2S_LEAVE: {
                struct s2s_leave *leave_msg = (struct s2s_leave *)buffer;
                
                log_message(&server_addr, &client_addr, "recv", "S2S Leave", leave_msg->req_channel, NULL, NULL);


                remove_neighbor_from_channel(leave_msg->req_channel, &client_addr);

                // IF routing table exists for said channel AND there is only one neighbor AND that neighbor is the sender THEN leave
                struct routing_table *rt = find_rt_entry(leave_msg->req_channel);
                if (rt && rt->neighbor_count == 1 &&
                memcmp(&rt->subscribed_neighbors[0]->addr, &client_addr, sizeof(struct sockaddr_in)) == 0) {

                    // before leaving, we need to check if there are any local users in the channel. if not, we can leave
                    struct channel *ch = find_channel(leave_msg->req_channel);
                    if (ch && ch->user_count == 0) {
                        s2s_leave(leave_msg->req_channel);
                    }
                }
                break;
            }
            case S2S_SAY: {
                struct s2s_say *say_msg = (struct s2s_say *)buffer;

                log_message(&server_addr, &client_addr, "recv", "S2S Say", say_msg->req_channel, say_msg->req_username, say_msg->req_text);

                // check for dups
                if (isdup(say_msg->unique_id)) {
                    server_print("Duplicate message detected. Responding with S2S Leave.\n");
                    struct s2s_leave leave_msg;
                    leave_msg.req_type = S2S_LEAVE;
                    strncpy(leave_msg.req_channel, say_msg->req_channel, CHANNEL_MAX);

                    send_d(&leave_msg, sizeof(leave_msg), &client_addr);

                    log_message(&server_addr, &client_addr, "send", "S2S Leave", say_msg->req_channel, NULL, NULL);
                    break;
                }

                // broadcast message to local users if any
                struct channel *ch = find_channel(say_msg->req_channel);
                if (ch != NULL) {
                    struct text_say txt_say;
                    txt_say.txt_type = TXT_SAY;
                    strncpy(txt_say.txt_channel, say_msg->req_channel, CHANNEL_MAX);
                    strncpy(txt_say.txt_username, say_msg->req_username, USERNAME_MAX);
                    strncpy(txt_say.txt_text, say_msg->req_text, SAY_MAX);
                    broadcast(&txt_say, ch);
                }

                // fwd message to other neighbors except the sender
                struct routing_table *rt = find_rt_entry(say_msg->req_channel);
                if (rt != NULL) {
                    int forwarded = 0;
                    for (int i = 0; i < rt->neighbor_count; i++) {
                        struct neighbor *nbr = rt->subscribed_neighbors[i];

                        // skip sender
                        if (nbr->addr.sin_addr.s_addr == client_addr.sin_addr.s_addr &&
                            nbr->addr.sin_port == client_addr.sin_port) {
                            continue;
                        }

                        send_d(say_msg, sizeof(*say_msg), &nbr->addr);
                        log_message(&server_addr, &nbr->addr, "send", "S2S Say", say_msg->req_channel, say_msg->req_username, say_msg->req_text);
                        forwarded = 1;
                    }

                    // If the message was not forwarded and there are no local users, send S2S Leave
                    if (!forwarded && (ch == NULL || ch->user_count == 0)) {
                        struct s2s_leave leave_msg;
                        leave_msg.req_type = S2S_LEAVE;
                        strncpy(leave_msg.req_channel, say_msg->req_channel, CHANNEL_MAX);

                        send_d(&leave_msg, sizeof(leave_msg), &client_addr);

                        log_message(&server_addr, &client_addr, "send", "S2S Leave", say_msg->req_channel, NULL, NULL);

                        remove_neighbor_from_channel(say_msg->req_channel, &client_addr);

                        struct routing_table *rt = find_rt_entry(say_msg->req_channel);
                        if (rt != NULL && rt->neighbor_count == 0) {
                            if (ch == NULL || ch->user_count == 0) {
                                // remove routing table entry for the channel
                                delete_rt_entry(say_msg->req_channel);
                                server_print("Removed internal records of channel %s.\n", say_msg->req_channel);
                            }
                        }

                    }
                }
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

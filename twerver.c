#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <signal.h>
#include "socket.h"

#ifndef PORT
    #define PORT 50787
#endif

#define LISTEN_SIZE 5
#define WELCOME_MSG "Welcome to CSC209 Twitter! Enter your username: \r\n"
#define SEND_MSG "send"
#define SHOW_MSG "show"
#define FOLLOW_MSG "follow"
#define UNFOLLOW_MSG "unfollow"
#define BUF_SIZE 256
#define MSG_LIMIT 8
#define FOLLOW_LIMIT 5

struct client {
    int fd;
    struct in_addr ipaddr;
    char username[BUF_SIZE];
    char message[MSG_LIMIT][BUF_SIZE];
    struct client *following[FOLLOW_LIMIT]; // Clients this user is following
    struct client *followers[FOLLOW_LIMIT]; // Clients who follow this user
    char inbuf[BUF_SIZE]; // Used to hold input from the client
    char *in_ptr; // A pointer into inbuf to help with partial reads
    int total_read; //number of bytes in inbuf
    struct client *next;
};


// Provided functions. 
void add_client(struct client **clients, int fd, struct in_addr addr);
void remove_client(struct client **clients, int fd);

// These are some of the function prototypes that we used in our solution 
// You are not required to write functions that match these prototypes, but
// you may find them helpful when thinking about operations in your program.

// Send the message s to all clients in active_clients. 
int announce(struct client *clients, char *s){
    struct client *curr = clients;
    while (curr != NULL){
        if(write(curr->fd, s, strlen(s)) != strlen(s)){
            fprintf(stderr, "Write to client %s failed\n", inet_ntoa(curr->ipaddr));
            return curr->fd;
        }
        curr = curr->next;
    }
    return 0;
}

// Move client c from new_clients list to active_clients list. 
void activate_client(struct client *c, 
    struct client **active_clients_ptr, struct client **new_clients_ptr){
        struct client **p;
        for (p = new_clients_ptr; *p && (*p)->fd != c->fd; p = &(*p)->next);
        struct client *t = (*p)->next;
        *p = t;
        c->next = *active_clients_ptr;
        *active_clients_ptr = c;
    }

/*
 * Check whether the user_input is equal to any existing users's username or empty
 * if not valid, return 1. otherwise, return 0.
 */
int check_validity(struct client *active_clients_ptr, char *user_input){
    if (user_input[0] == '\0')
        return 1;

    for (struct client *p = active_clients_ptr; p != NULL; p = p->next){
        if (strcmp(p->username, user_input) == 0){
            return 1;
        }
    }
    return 0;
}

//find the network newline and return the index of '\r'
int find_network_newline(const char *buf, int n) {
    for (int i = 0; i < n - 1; i++){
        if (buf[i] == '\r' && buf[i + 1] == '\n'){
            return i + 2;
        } 
    }
    return -1;
}

/**
 * read from the user
 * return -1 if full line is read
 * return fd if client is closed
 * otherwise return 0
 */
int partial_read(struct client *p, char *user_input){

    int nbytes, where;
    int room = sizeof(p->inbuf);

    nbytes = read(p->fd, p->in_ptr, room);
    if(nbytes < 1){
        fprintf(stderr, "read client %d failed\n", p->fd);
        return p->fd;
    }

    p->total_read += nbytes;
    printf("[%d] Read %d bytes\n", p->fd, nbytes);

    where = find_network_newline(p->inbuf, p->total_read);
    if (where > 0){
        p->inbuf[where - 2] = '\0';
        strcpy(user_input, p->inbuf);
        printf("[%d] Found newline: %s\n", p->fd, user_input);
        memmove(p->inbuf, p->inbuf + where, BUF_SIZE - where);
        p->total_read = p->total_read - where;
        p->in_ptr = p->inbuf + p->total_read; 
        room = BUF_SIZE - p->total_read;
        return -1;
    }
    p->in_ptr = p->inbuf + p->total_read; 
    room = BUF_SIZE - p->total_read;
    
    return 0;
}

/**
 * cur_fd client follow the com_input user if is an active client.
 * return cur_fd if client is closed
 */
int follow_command(struct client *active_client, int cur_fd, struct client *p, char *com_input){
    struct client *curr = NULL;
    //find the active client with username
    for (curr = active_client; curr != NULL && strcmp(curr->username, com_input + 1) != 0; curr = curr->next);
    // the username is not in the active clinet
    if (curr ==  NULL){
        printf("%s is not an active client\n", com_input + 1);
        char user_not_active[BUF_SIZE * 2];
        sprintf(user_not_active, "%s is not an active client\r\n", com_input + 1);
        if (write(cur_fd, user_not_active, strlen(user_not_active)) != strlen(user_not_active)){
                fprintf(stderr, "Write to client %s failed\n", inet_ntoa(p->ipaddr));
                return cur_fd;
        }
    } else {
        int j, k;
        for (j = 0; curr->followers[j] != NULL && j < FOLLOW_LIMIT; j++);
        for (k = 0; p->following[k] != NULL && k < FOLLOW_LIMIT; k++);
        //following is successful
        if (j < FOLLOW_LIMIT && k < FOLLOW_LIMIT){
            curr->followers[j] = p;
            p->following[k] = curr;
            printf("%s is now following %s\n", p->username, com_input + 1);

        //following is unsuccessful
        } else {
            printf("following is not successful due to follow limit\n");
            char *follow_msg = "following is not successful due to follow limit\r\n";
            if (write(cur_fd, follow_msg, strlen(follow_msg)) != strlen(follow_msg)){
                fprintf(stderr, "Write to client %s failed\n", inet_ntoa(p->ipaddr));
                return cur_fd;
            }
        }
    }
    return 0;
}

/**
 * cur_fd client unfollow the com_input user if is an active client.
 * return cur_fd if client is closed
 */
int unfollow_command(struct client *active_client, int cur_fd, struct client *p, char *com_input){
    struct client *curr = NULL;
    //find the active client with username
    for (curr = active_client; curr != NULL && strcmp(curr->username, com_input + 1) != 0; curr = curr->next);
    // the username is not an active client
    if (curr ==  NULL){
        printf("%s is not an active client\n", com_input + 1);
        char user_not_active[BUF_SIZE * 2];
        sprintf(user_not_active, "%s is not an active client\r\n", com_input + 1);
        if (write(cur_fd, user_not_active, strlen(user_not_active)) != strlen(user_not_active)){
            fprintf(stderr, "Write to client %s failed\n", inet_ntoa(p->ipaddr));
            return cur_fd;
        }
    } else {
        int j, k;
        for (j = 0; curr->followers[j] != p && j < FOLLOW_LIMIT; j++);
        for (k = 0; p->following[k] != curr && k < FOLLOW_LIMIT; k++);
        //unfollowing is successful
        if (j < FOLLOW_LIMIT && k < FOLLOW_LIMIT){
            curr->followers[j] = NULL;
            p->following[k] = NULL;
            printf("%s is now unfollowing %s\n", p->username, com_input + 1);

        //unfollowing is unsuccessful
        } else {
            printf("unfollowing is not successful\n");
            char *follow_msg = "unfollowing is not successful\r\n";
            if (write(cur_fd, follow_msg, strlen(follow_msg)) != strlen(follow_msg)){
                fprintf(stderr, "Write to client %s failed\n", inet_ntoa(p->ipaddr));
                return cur_fd;
            }
        }
    }
    return 0;
}

/**
 * client send messaege to all followers, and stores message into attribute
 * return the fd of follower if follower is closed
 */
int send_command(struct client *p, char *com_input){
    int index;
    for(index = 0; strlen(p->message[index]) != 0 && index < MSG_LIMIT; index++);

    // send is successful
    if (index < MSG_LIMIT){
        strcpy(p->message[index], com_input + 1);
        for (int i = 0; i < FOLLOW_LIMIT; i++){
            char send_msg[BUF_SIZE * 2];
            sprintf(send_msg, "%s: %s\r\n", p->username, com_input + 1);
            if (p->followers[i] != NULL){
                if (write(p->followers[i]->fd, send_msg, strlen(send_msg)) != strlen(send_msg)){
                    fprintf(stderr, "Write to client %s failed\n", inet_ntoa(p->followers[i]->ipaddr));
                    return p->followers[i]->fd;
                }
            }
        }
    //send is unsuccesful
    } else {
        printf("%s already has over MSG_LIMIT messagges\n", p->username);
        char *send_alert = "send is unsuccesuful due to message limit\r\n";
        if (write(p->fd, send_alert, strlen(send_alert)) != strlen(send_alert)){
            fprintf(stderr, "Write to client %s failed\n", inet_ntoa(p->ipaddr));
            return p->fd;
        }
    }
    return 0;
}

/**
 * shows all messages sent by clients the p is following
 * return fd if the client is closed
 */
int show_command(struct client *p){
    for (int i = 0; i < FOLLOW_LIMIT; i++){
        if (p->following[i] != NULL){
            for (int j = 0; j < MSG_LIMIT; j++){
                if (strlen(p->following[i]->message[j]) != 0){
                    char show_meg[BUF_SIZE * 2];
                    sprintf(show_meg, "%s wrote: %s\r\n", p->following[i]->username, p->following[i]->message[j]);
                    if (write(p->fd, show_meg, strlen(show_meg)) != strlen(show_meg)){
                        fprintf(stderr, "Write to client %s failed\n", inet_ntoa(p->ipaddr));
                        return p->fd;
                    }
                }
            }
        }
    }
    return 0;
}

// The set of socket descriptors for select to monitor.
// This is a global variable because we need to remove socket descriptors
// from allset when a write to a socket fails. 
fd_set allset;

/* 
 * Create a new client, initialize it, and add it to the head of the linked
 * list.
 */
void add_client(struct client **clients, int fd, struct in_addr addr) {
    struct client *p = malloc(sizeof(struct client));
    if (!p) {
        perror("malloc");
        exit(1);
    }

    printf("Adding client %s\n", inet_ntoa(addr));
    p->fd = fd;
    p->ipaddr = addr;
    p->username[0] = '\0';
    p->in_ptr = p->inbuf;
    p->inbuf[0] = '\0';
    p->total_read = 0;
    p->next = *clients;

    // initialize messages to empty strings
    for (int i = 0; i < MSG_LIMIT; i++) {
        p->message[i][0] = '\0';
    }

    // initialize follower/following to empty list
    for (int j = 0; j < FOLLOW_LIMIT; j++){
        p->followers[j] = NULL;
        p->following[j] = NULL;
    }

    *clients = p;
}

/* 
 * Remove client from the linked list and close its socket.
 * Also, remove socket descriptor from allset.
 */
void remove_client(struct client **clients, int fd) {
    struct client **p;

    for (p = clients; *p && (*p)->fd != fd; p = &(*p)->next);

    // Now, p points to (1) top, or (2) a pointer to another client
    // This avoids a special case for removing the head of the list
    if (*p) {
        // TODO: Remove the client from other clients' following/followers
        // lists
        for(int i = 0; i < FOLLOW_LIMIT; i++){
            struct client *p_following = (*p)->following[i];
            struct client *p_followers = (*p)->followers[i];
            for(int j = 0; j < FOLLOW_LIMIT; j++){
                if (p_following != NULL && p_following->followers[j] == *p){
                    printf("%s is no longer following %s because disconnected\n", (*p)->username, p_following->username);
                    p_following->followers[j] = NULL;
                }
                if (p_followers != NULL && p_followers->following[j] == *p){
                    printf("%s no longer has %s as a follower\n", (*p)->username, p_followers->username);
                    p_followers->following[j] = NULL;
                }
            }
        }

        //announce to all active client if p is active
        if ((*p)->username[0] != '\0'){
            char leave_mag[BUF_SIZE * 2];
            sprintf(leave_mag, "Goodbye %s\t\n", (*p)->username);
            announce(*clients, leave_mag);
        }
        
        // Remove the client
        struct client *t = (*p)->next;
        printf("Removing client %d %s\n", fd, inet_ntoa((*p)->ipaddr));
        FD_CLR((*p)->fd, &allset);
        close((*p)->fd);
        free(*p);
        *p = t;
    } else {
        fprintf(stderr, 
            "Trying to remove fd %d, but I don't know about it\n", fd);
    }
}


int main (int argc, char **argv) {
    int clientfd, maxfd, nready;
    struct client *p;
    struct sockaddr_in q;
    fd_set rset;

    // If the server writes to a socket that has been closed, the SIGPIPE
    // signal is sent and the process is terminated. To prevent the server
    // from terminating, ignore the SIGPIPE signal. 
    struct sigaction sa;
    sa.sa_handler = SIG_IGN;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGPIPE, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }

    // A list of active clients (who have already entered their names). 
    struct client *active_clients = NULL;

    // A list of clients who have not yet entered their names. This list is
    // kept separate from the list of active clients, because until a client
    // has entered their name, they should not issue commands or 
    // or receive announcements. 
    struct client *new_clients = NULL;

    struct sockaddr_in *server = init_server_addr(PORT);
    int listenfd = set_up_server_socket(server, LISTEN_SIZE);
    free(server);

    // Initialize allset and add listenfd to the set of file descriptors
    // passed into select 
    FD_ZERO(&allset);
    FD_SET(listenfd, &allset);

    // maxfd identifies how far into the set to search
    maxfd = listenfd;

    while (1) {
        // make a copy of the set before we pass it into select
        rset = allset;

        nready = select(maxfd + 1, &rset, NULL, NULL, NULL);
        if (nready == -1) {
            perror("select");
            exit(1);
        } else if (nready == 0) {
            continue;
        }

        // check if a new client is connecting
        if (FD_ISSET(listenfd, &rset)) {
            printf("A new client is connecting\n");
            clientfd = accept_connection(listenfd, &q);

            FD_SET(clientfd, &allset);
            if (clientfd > maxfd) {
                maxfd = clientfd;
            }
            printf("Connection from %s\n", inet_ntoa(q.sin_addr));
            add_client(&new_clients, clientfd, q.sin_addr);
            char *greeting = WELCOME_MSG;
            if (write(clientfd, greeting, strlen(greeting)) == -1) {
                fprintf(stderr, 
                    "Write to client %s failed\n", inet_ntoa(q.sin_addr));
                remove_client(&new_clients, clientfd);
            }
        }

        // Check which other socket descriptors have something ready to read.
        // The reason we iterate over the rset descriptors at the top level and
        // search through the two lists of clients each time is that it is
        // possible that a client will be removed in the middle of one of the
        // operations. This is also why we call break after handling the input.
        // If a client has been removed, the loop variables may no longer be 
        // valid.
        int cur_fd, handled;
        for (cur_fd = 0; cur_fd <= maxfd; cur_fd++) {
            if (FD_ISSET(cur_fd, &rset)) {
                handled = 0;
                int client_closed;
                char user_input[BUF_SIZE];

                // Check if any new clients are entering their names
                for (p = new_clients; p != NULL; p = p->next) {
                    if (cur_fd == p->fd) {
                        // TODO: handle input from a new client who has not yet
                        // entered an acceptable name
                        handled = 1;

                        //read username from the new client
                        client_closed = partial_read(p, user_input);
                        
                        if (client_closed == -1){
                            //check the validity of username
                            if(check_validity(active_clients, user_input) == 1){
                                printf("username is not valid\n");
                                char *invalid = "Your username is not valid, Please enter again: \r\n";

                                if (write(cur_fd, invalid, strlen(invalid)) != strlen(invalid)) {
                                    fprintf(stderr, "Write to client %s failed\n", inet_ntoa(q.sin_addr));
                                    client_closed = cur_fd;
                                }
                            } else {
                                strcpy(p->username, user_input);
                                activate_client(p, &active_clients, &new_clients);
                                char join_message[BUF_SIZE * 2];
                                sprintf(join_message, "%s has just joined\r\n", p->username);
                                client_closed = announce(active_clients, join_message);
                                printf("%s has just joined\n", p->username);
                            }
                        }   

                        if (client_closed > 0){
                            remove_client(&new_clients, client_closed);
                            client_closed = 0;
                        }
                        break;
                    }
                }

                
                if (!handled) {
                    // Check if this socket descriptor is an active client
                    for (p = active_clients; p != NULL; p = p->next) {
                        if (cur_fd == p->fd) {
                            // TODO: handle input from an active client
                            client_closed = partial_read(p, user_input);

                            //full line is read
                            if (client_closed == -1){

                                printf("%s: %s\n", p->username, user_input);
                                char *com_input;
                                //check the command
                                if ((com_input = strchr(user_input, ' ')) != NULL){
                                    
                                    int com_length = strlen(user_input) - strlen(com_input);

                                    //user entered follow command       
                                    if (com_length == strlen(FOLLOW_MSG) && strncmp(user_input, FOLLOW_MSG, com_length) == 0){
                                        client_closed = follow_command(active_clients, cur_fd, p, com_input);
                                    }

                                    //user entered unfollow command
                                    else if (com_length == strlen(UNFOLLOW_MSG) && strncmp(user_input, UNFOLLOW_MSG, com_length) == 0){
                                        client_closed = unfollow_command(active_clients, cur_fd, p, com_input);
                                    } 

                                    //user entered send command
                                    else if (com_length == strlen(SEND_MSG) && strncmp(user_input, SEND_MSG, com_length) == 0){
                                        client_closed = send_command(p, com_input);
                                    }

                                    //user entered invalid command
                                    else {
                                        printf("Invalid command\n");
                                        char *invalid_msg =  "Invalid command\r\n";
                                        if (write(cur_fd, invalid_msg, strlen(invalid_msg)) != strlen(invalid_msg)){
                                            fprintf(stderr, "Write to client %s failed\n", inet_ntoa(p->ipaddr));
                                            client_closed = cur_fd;
                                        }
                                    }
                                }

                                //user entered show command 
                                else if (strcmp(user_input, SHOW_MSG) == 0){
                                    client_closed = show_command(p);
                                }

                                //user entered quit command
                                else if (strcmp(user_input, "quit") == 0){
                                    printf("Disconnect from %s\n", inet_ntoa(p->ipaddr));
                                    remove_client(&active_clients, cur_fd);
                                }

                                //user entered invalid command
                                else {
                                    printf("Invalid command\n");
                                    char *invalid_msg =  "Invalid command\r\n";
                                    if (write(cur_fd, invalid_msg, strlen(invalid_msg)) != strlen(invalid_msg)){
                                        fprintf(stderr, "Write to client %s failed\n", inet_ntoa(p->ipaddr));
                                        remove_client(&active_clients, cur_fd);
                                    }
                                }
                            }

                            //remove the client if there is a closed client
                            if (client_closed > 0){
                                remove_client(&active_clients, client_closed);
                                client_closed = 0;
                            }
                            break;
                        }
                    }
                }
            }
        }
    }
    return 0;
}

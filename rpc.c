#define _POSIX_C_SOURCE 200112L

#include "rpc.h"
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>

#define BACKLOG 10
#define PORT_LEN 6

typedef struct registered_function {
    char *name;
    rpc_handler handler;
    struct registered_function *next;
} registered_function;

struct rpc_server {
    int socket_fd;                  // socket file descriptor
    int new_fd;                     // new file descriptor for accept()
    int port;                       // port number
    registered_function *functions; // linked list of registered functions
};

rpc_server *rpc_init_server(int port) {
    rpc_server *server = (rpc_server *)malloc(sizeof(rpc_server));
    assert(server);

    int status;
    struct addrinfo hints;
    struct addrinfo *res;
    char port_str[PORT_LEN];

    // convert int port to string
    snprintf(port_str, sizeof(port_str), "%d", port);

    // load up address structs with getaddrinfo(), referencing Beej's guide
    memset(&hints, 0, sizeof hints); // ensure the struct is empty
    hints.ai_family = AF_INET6;      // set the struct to be IPv6
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if ((status = getaddrinfo(NULL, port, &hints, &res)) != 0) {
        fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
        return NULL;
    }

    // create a socket
    server->socket_fd =
        socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (server->socket_fd == -1) {
        perror("socket");
        return NULL;
    }

    // bind the socket to the port
    if (bind(server->socket_fd, res->ai_addr, res->ai_addrlen) == -1) {
        perror("bind");
        return NULL;
    }

    // listen for connections
    if (listen(server->socket_fd, BACKLOG) == -1) {
        perror("listen");
        return NULL;
    }

    server->functions = NULL;
    freeaddrinfo(res);

    return server;
}

int rpc_register(rpc_server *srv, char *name, rpc_handler handler) {
    return -1;
}

void rpc_serve_all(rpc_server *srv) {}

struct rpc_client {
    /* Add variable(s) for client state */
};

struct rpc_handle {
    /* Add variable(s) for handle */
};

rpc_client *rpc_init_client(char *addr, int port) {
    return NULL;
}

rpc_handle *rpc_find(rpc_client *cl, char *name) {
    return NULL;
}

rpc_data *rpc_call(rpc_client *cl, rpc_handle *h, rpc_data *payload) {
    return NULL;
}

void rpc_close_client(rpc_client *cl) {}

void rpc_data_free(rpc_data *data) {
    if (data == NULL) {
        return;
    }
    if (data->data2 != NULL) {
        free(data->data2);
    }
    free(data);
}

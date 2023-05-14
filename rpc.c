#define _POSIX_C_SOURCE 200112L

#include "rpc.h"
#include <assert.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>

#define BACKLOG 10
#define PORT_LEN 6
#define NAME_LIMIT 1000

typedef struct registered_function {
    char *name;                       // name of the remote procedure
    rpc_handler handler;              // function to the remote procedure
    struct registered_function *next; // pointer to the next function
} registered_function;

struct rpc_server {
    int socket_fd;                  // socket file descriptor
    int new_fd;                     // new file descriptor for accept()
    int port;                       // port number
    registered_function *functions; // linked list of registered functions
};

rpc_server *rpc_init_server(int port) {
    rpc_server *server = (rpc_server *)malloc(sizeof(rpc_server));
    if (server == NULL) {
        return NULL;
    }

    // initialize server attributes
    server->port = port;
    server->new_fd = -1;
    server->functions = NULL;

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

    if ((status = getaddrinfo(NULL, port_str, &hints, &res)) != 0) {
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

    printf("server: waiting for connections on port %d\n", server->port);

    freeaddrinfo(res);

    return server;
}

/* helper functions for rpc_register */

/**
 * The function adds a registered function to a given RPC server.
 *
 * @param srv A pointer to an instance of the rpc_server struct, which
 * represents an RPC server.
 * @param function A pointer to a struct representing a registered function that
 * will be added to the server's list of available functions.
 */
static void add_function_to_server(rpc_server *srv,
                                   registered_function *function) {
    function->next = srv->functions;
    srv->functions = function;
}

/**
 * Creates a new registered function with a given name and handler and returns
 * a pointer to it.
 *
 * @param name A string representing the name of the function being registered.
 * @param handler The `handler` parameter is a function pointer to the RPC
 * handler function that will be associated with the registered function.
 * This handler function will be called when the registered function is invoked
 * remotely.
 *
 * @return a pointer to a newly created `registered_function` struct.
 */
static registered_function *create_function(char *name, rpc_handler handler) {
    registered_function *new_function =
        (registered_function *)malloc(sizeof(registered_function));

    if (new_function == NULL) {
        return NULL;
    }

    new_function->name = (char *)malloc(NAME_LIMIT + 1 * sizeof(char));
    if (new_function->name == NULL) {
        free(new_function);
        return NULL;
    }

    strncpy(new_function->name, name, NAME_LIMIT);
    new_function->name[NAME_LIMIT] = '\0';

    new_function->handler = handler;
    new_function->next = NULL;

    return new_function;
}

/* testing */
void debug_print_registered_functions(rpc_server *srv) {
    registered_function *current_function = srv->functions;

    while (current_function != NULL) {
        printf("Registered function: %s\n", current_function->name);
        current_function = current_function->next;
    }
}
/* testing */

int rpc_register(rpc_server *srv, char *name, rpc_handler handler) {
    // handle null arguments
    if (srv == NULL || name == NULL || handler == NULL) {
        return -1;
    }

    registered_function *new_function = create_function(name, handler);
    if (new_function == NULL) {
        return -1;
    }

    add_function_to_server(srv, new_function);

    /* testing */
    debug_print_registered_functions(srv);
    /* testing */

    return 0;
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

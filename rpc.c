#define _POSIX_C_SOURCE 200809L

#include "rpc.h"
#include <arpa/inet.h>
#include <assert.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define BACKLOG 10
#define PORT_LEN 6
#define NAME_LIMIT 1000
#define BUFFER_SIZE 2048

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

/**
 * Initialize the server by setting up a socket for the server and starts
 * listening for incoming connections
 */
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
static void add_function_to_server(rpc_server *server,
                                   registered_function *new_function) {
    registered_function *current_function = server->functions;
    registered_function *previous_function = NULL;

    while (current_function != NULL) {
        if (strcmp(current_function->name, new_function->name) == 0) {
            if (previous_function == NULL) {
                server->functions = current_function->next;
            } else {
                // link previous node and next node of current
                previous_function->next = current_function->next;
                printf("%s has been updated!\n", current_function->name);
            }

            // remove old function from list
            free(current_function->name);
            free(current_function);

            break;
        }

        previous_function = current_function;
        current_function = current_function->next;
    }

    // add new function to head of list
    new_function->next = server->functions;
    server->functions = new_function;
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

    printf("\n");
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

/**
 * From Beej's guide: helper function to get sockaddr, IPv4 or IPv6
 */
static void *get_in_addr(struct sockaddr *sa) {
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in *)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6 *)sa)->sin6_addr);
}

/**
 * Helper function to find function in server's registered functions
 */
static int function_registered(rpc_server *server, char *function_name) {
    registered_function *current_function = server->functions;

    while (current_function != NULL) {
        if (strcmp(current_function->name, function_name) == 0) {
            printf("function %s is found in the server\n",
                   current_function->name);
            return true;
        }
        current_function = current_function->next;
    }

    return false;
}

/**
 * Function runs on the server side, continuously accepts incoming connections,
 * receives requests from clients
 */
void rpc_serve_all(rpc_server *srv) {
    if (srv == NULL) {
        return;
    }

    /* reference from Beej's guide */
    struct sockaddr_storage their_addr;
    socklen_t addr_size;
    char s[INET6_ADDRSTRLEN];
    socklen_t sin_size;

    while (1) {
        // accept an incoming connection
        addr_size = sizeof their_addr;
        srv->new_fd =
            accept(srv->socket_fd, (struct sockaddr *)&their_addr, &addr_size);

        if (srv->new_fd == -1) {
            perror("accept");
            continue;
        }

        // convert client IP to string
        inet_ntop(their_addr.ss_family,
                  get_in_addr((struct sockaddr *)&their_addr), s, sizeof s);

        printf("server: got connection from %s\n", s);

        if (!fork()) {
            close(srv->socket_fd);

            rpc_data payload;
            char buf[BUFFER_SIZE];
            int byte_num;

            // receive payload struct and fixed size
            if (recv(srv->new_fd, &payload, sizeof(payload), 0) == -1) {
                perror("recv");
                return;
            }

            // allocate memory for payload function name
            char *function_name = malloc(payload.data1 + 1);
            int name_length = payload.data1;

            // receive function name from payload
            if (recv(srv->new_fd, function_name, name_length, 0) == -1) {
                perror("recv");
                free(function_name);
                return;
            }

            function_name[payload.data1] = '\0';

            printf("server: received function name '%s'\n", function_name);

            // check if function is registered
            rpc_data response;
            if (function_registered(srv, function_name)) {
                response.data1 = true;
            } else {
                response.data1 = false;
            }

            // send response to client
            if (send(srv->new_fd, &response, sizeof(response), 0) == -1) {
                perror("send");
                free(function_name);
                return;
            }

            free(function_name);
            close(srv->new_fd);
            exit(0);
        }

        close(srv->new_fd);
    }
}

struct rpc_client {
    char *server_address;
    int socket_fd;
    int port;
};

struct rpc_handle {
    char *function_name;
    rpc_client *client;
};

/**
 * Initialize the client by setting up address and port to make requests to the
 * server
 */
rpc_client *rpc_init_client(char *addr, int port) {
    rpc_client *client = (rpc_client *)malloc(sizeof(rpc_client));
    if (client == NULL) {
        return NULL;
    }

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

    if ((status = getaddrinfo(addr, port_str, &hints, &res)) != 0) {
        fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
        return NULL;
    }

    // create a socket
    client->socket_fd =
        socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (client->socket_fd == -1) {
        perror("socket");
        return NULL;
    }

    if (connect(client->socket_fd, res->ai_addr, res->ai_addrlen) == -1) {
        perror("connect");
        free(client);
        return NULL;
    }

    freeaddrinfo(res);

    return client;
}

/**
 * Function runs on the client side.
 * Uses information stored in rpc_client struct to send a request to the server
 * and receive a response
 */
rpc_handle *rpc_find(rpc_client *cl, char *name) {
    if (cl == NULL || name == NULL) {
        return NULL;
    }

    // initialize payload: purpose of data1 is to pass int to avoid memory
    // management issues by setting data2_len = 0 and data2 = NULL */
    rpc_data payload;
    payload.data1 = strlen(name);
    payload.data2 = NULL;
    payload.data2_len = 0;

    /* The above code is sending the contents of the variable `payload` over a
    socket connection using the `send()` function. The function returns the
    number of bytes sent, or -1 if an error occurred. */
    if (send(cl->socket_fd, &payload, sizeof(payload), 0) == -1) {
        perror("send");
        return NULL;
    }

    // The above code is sending data through a socket connection using the
    // send() function. */
    if (send(cl->socket_fd, name, payload.data1, 0) == -1) {
        perror("send");
        return NULL;
    }

    // server receives the request and checks for function in register
    rpc_data response;

    if (recv(cl->socket_fd, &response, sizeof(response), 0) == -1) {
        perror("receive");
        return NULL;
    }

    // response true if function exists in server registered functions
    if (response.data1 == false) {
        return NULL;
    } else {
        rpc_handle *function_handle = (rpc_handle *)malloc(sizeof(rpc_handle));
        function_handle->function_name = strdup(name);
        return function_handle;
    }
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

#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

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
#define MAX_FUNCTIONS 10
#define NOT_FOUND -1

typedef struct registered_function {
    char *name;          // name of the remote procedure
    rpc_handler handler; // function to the remote procedure
} registered_function;

struct rpc_server {
    int socket_fd; // socket file descriptor
    int new_fd;    // new file descriptor for accept()
    int port;      // port number
    registered_function
        functions[MAX_FUNCTIONS]; // array of registered functions
    int functions_count;          // number of registered functions
};

struct rpc_client {
    char *server_address;
    int socket_fd;
    int port;
};

struct rpc_handle {
    int index;
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
    server->functions_count = 0;

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

    freeaddrinfo(res);

    return server;
}

/* Function to add a registered function to the server */
static void add_function_to_server(rpc_server *server,
                                   registered_function *new_function) {
    for (int i = 0; i < server->functions_count; i++) {
        if (strcmp(server->functions[i].name, new_function->name) == 0) {
            // Function with the same name already exists, replace it
            server->functions[i] = *new_function;
            return;
        }
    }

    // If the function does not exist, add it to the array
    if (server->functions_count < MAX_FUNCTIONS) {
        server->functions[server->functions_count] = *new_function;
        server->functions_count++;
    } else {
        fprintf(stderr, "Maximum number of registered functions reached.\n");
    }
}

/* Creates a new registered function with a given name and handler */
static registered_function *create_function(char *name, rpc_handler handler) {
    registered_function *new_function =
        (registered_function *)malloc(sizeof(registered_function));

    if (new_function == NULL) {
        return NULL;
    }

    new_function->name = strdup(name);
    new_function->handler = handler;

    return new_function;
}

/* Registers a new RPC handler with a given server */
int rpc_register(rpc_server *srv, char *name, rpc_handler handler) {
    if (srv == NULL || name == NULL || handler == NULL) {
        return -1;
    }

    registered_function *new_function = create_function(name, handler);
    if (new_function == NULL) {
        return -1;
    }

    add_function_to_server(srv, new_function);

    return 0;
}

/* From Beej's guide: helper function to get sockaddr, IPv4 or IPv6 */
static void *get_in_addr(struct sockaddr *sa) {
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in *)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6 *)sa)->sin6_addr);
}

/**
 * Finds the index of a registered function in a given RPC server by comparing
 * its name to a given function name.
 */
static int find_function_index(rpc_server *server, char *function_name) {
    registered_function *functions = server->functions;

    for (int index = 0; index < server->functions_count; index++) {
        if (strcmp(functions[index].name, function_name) == 0) {
            return index;
        }
    }

    return NOT_FOUND;
}

/**
 * Handles a lookup request by receiving a function name, finding its
 * index, and sending the index back to the client.
 */
static void handle_lookup_request(rpc_server *srv, rpc_data *request,
                                  int socket_fd) {
    char *function_name = malloc(request->data1 + 1);

    if (recv(socket_fd, function_name, request->data1, 0) == -1) {
        perror("recv");
        free(function_name);
    }
    function_name[request->data1] = '\0';

    int function_index = find_function_index(srv, function_name);
    uint64_t data_function_index;

    if (function_index != NOT_FOUND) {
        data_function_index = htobe64((uint64_t)function_index);
    } else {
        data_function_index = htobe64((uint64_t)-1);
    }

    // send the function index directly
    if (send(socket_fd, &data_function_index, sizeof(uint64_t), 0) == -1) {
        perror("send");
    }

    free(function_name);
}

static void handle_function_invocation(rpc_server *srv, rpc_data *request,
                                       int socket_fd) {
    char *data2 = malloc(request->data2_len);
    if (data2 == NULL) {
        perror("malloc");
    }

    if (recv(socket_fd, data2, request->data2_len, 0) == -1) {
        perror("recv");
        free(data2);
    }

    request->data2 = data2;

    // receive function index in network byte order
    u_int64_t function_index_network_order;

    // receive function_index
    if (recv(socket_fd, &function_index_network_order, sizeof(u_int64_t), 0) ==
        -1) {
        perror("recv");
    }

    // convert network byte to host byte
    int function_index = (int)ntohl(function_index_network_order);

    // check if function_index is valid
    if (function_index < 0 || function_index >= srv->functions_count) {
        perror("index");
    }

    rpc_handler *function = &(srv->functions[function_index].handler);

    // Check if function is NULL
    if (function == NULL) {
        free(data2);
    }

    rpc_data *response = (*function)(request);

    rpc_data error_response;
    if ((response == NULL) ||
        (response->data2 == NULL && response->data2_len > 0) ||
        (response->data2 != NULL && response->data2_len == 0)) {
        error_response.data1 = -1;
        error_response.data2 = strdup("Function failed");
        error_response.data2_len = strlen(error_response.data2) + 1;
        response = &error_response;
    }

    // prepare the buffer for serialization
    size_t buffer_size = sizeof(uint64_t) + sizeof(size_t) +
                         (response->data2 ? response->data2_len : 0);
    char *buffer = malloc(buffer_size);
    if (!buffer) {
        perror("malloc");
        free(data2);
        free(response);
    }

    size_t offset = 0;

    // serialize the response struct into the buffer
    uint64_t data1_network = htobe64(response->data1);
    memcpy(buffer + offset, &data1_network, sizeof(uint64_t));
    offset += sizeof(uint64_t);

    uint32_t data2_len_network = htonl(response->data2_len);
    memcpy(buffer + offset, &data2_len_network, sizeof(uint32_t));
    offset += sizeof(uint32_t);

    // if data2 is not NULL, copy it into the buffer as well
    if (response->data2) {
        memcpy(buffer + offset, response->data2, response->data2_len);
    }

    // send the serialized data to the client
    if (send(socket_fd, buffer, buffer_size, 0) == -1) {
        perror("send");
        free(data2);
        free(response);
        free(buffer);

        if (response != &error_response) {
            free(response);
        }
    }

    free(buffer);
    free(data2);
}

/**
 * function runs on the server side, continuously accepts incoming
 * connections, receives requests from clients
 */
void rpc_serve_all(rpc_server *srv) {
    if (srv == NULL) {
        return;
    }

    /* reference from Beej's guide */
    struct sockaddr_storage their_addr;
    socklen_t addr_size;
    char s[INET6_ADDRSTRLEN];

    while (1) {

        // accept an incoming connection
        addr_size = sizeof(their_addr);
        srv->new_fd =
            accept(srv->socket_fd, (struct sockaddr *)&their_addr, &addr_size);
        if (srv->new_fd == -1) {
            perror("accept");
            continue;
        }

        // convert client IP to string
        inet_ntop(their_addr.ss_family,
                  get_in_addr((struct sockaddr *)&their_addr), s, sizeof(s));

        while (1) {

            // receive request byte stream
            size_t request_len = sizeof(rpc_data);
            char request_buffer[request_len];
            ssize_t bytes_received =
                recv(srv->new_fd, request_buffer, request_len, 0);
            if (bytes_received <= 0) {
                break;
            }

            // copy the received byte stream into an rpc_data struct
            rpc_data request;
            size_t offset = 0;

            // deserialize data1 and data2_len from network byte order to host
            // byte order
            uint64_t data1_net;
            uint32_t data2_len_net;

            memcpy(&data1_net, request_buffer + offset, sizeof(data1_net));
            offset += sizeof(data1_net);

            memcpy(&data2_len_net, request_buffer + offset,
                   sizeof(data2_len_net));
            offset += sizeof(data2_len_net);

            request.data1 = be64toh(data1_net);
            request.data2_len = ntohl(data2_len_net);

            if (request.data2_len == 0) {

                handle_lookup_request(srv, &request, srv->new_fd);

            } else {

                handle_function_invocation(srv, &request, srv->new_fd);
            }

            // // Send the response data1 to the client
            // if (response != NULL) {
            //     printf("\tresponse: %d\n", response->data1);
            //     if (send(srv->new_fd, &(response->data1), sizeof(int), 0) ==
            //         -1) {
            //         perror("send");
            //     }

            //     free(response);
            // }
        }

        close(srv->new_fd);
    }
}

/**
 * Initialize the client by setting up address and port to make requests to
 * the server
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

static char *serialize_payload(rpc_data *request) {
    // prepare the payload byte stream
    size_t payload_len = sizeof(rpc_data);
    char *payload = (char *)malloc(payload_len);

    if (!payload) {
        perror("malloc");
        return NULL;
    }

    size_t offset = 0;

    // copy the data1 and data2_len into payload with network byte order
    uint64_t data1_net = htobe64(request->data1);
    uint32_t data2_len_net = htonl(request->data2_len);

    memcpy(payload + offset, &data1_net, sizeof(data1_net));
    offset += sizeof(data1_net);

    memcpy(payload + offset, &data2_len_net, sizeof(data2_len_net));
    offset += sizeof(data2_len_net);

    return payload;
}

rpc_handle *rpc_find(rpc_client *cl, char *name) {
    if (cl == NULL || name == NULL) {
        return NULL;
    }

    size_t name_len = strlen(name);

    // prepare the request struct
    rpc_data request = {.data1 = name_len, .data2 = NULL, .data2_len = 0};

    // serialize the request struct and name into payload
    char *payload = serialize_payload(&request);
    if (payload == NULL) {
        return NULL;
    }

    size_t payload_len = sizeof(rpc_data);

    // send the payload
    if (send(cl->socket_fd, payload, payload_len, 0) == -1) {
        perror("send");
        free(payload);
        return NULL;
    }

    // send the function name
    if (send(cl->socket_fd, name, name_len, 0) == -1) {
        perror("send");
        return NULL;
    }

    free(payload);

    // prepare a variable to hold the received data
    uint64_t received_data;

    // receive the data
    if (recv(cl->socket_fd, &received_data, sizeof(uint64_t), 0) == -1) {
        perror("recv");
        return NULL;
    }

    // convert big endian to host byte order
    received_data = be64toh(received_data);

    uint64_t invalid_response = (uint64_t)-1;
    if (received_data == invalid_response) {
        return NULL;
    }

    rpc_handle *function_handle = (rpc_handle *)malloc(sizeof(rpc_handle));
    if (!function_handle) {
        perror("malloc");
        return NULL;
    }

    function_handle->index = received_data;

    return function_handle;
}

rpc_data *rpc_call(rpc_client *cl, rpc_handle *h, rpc_data *payload) {
    if (cl == NULL || h == NULL || payload == NULL) {
        return NULL;
    }

    if (payload->data2_len >= 100000) {
        fprintf(stderr, "Overlength error");
        return NULL;
    }

    // inconsistent call data2 null and data2_len is non-zero and vice-versa
    if (payload->data2 == NULL && payload->data2_len != 0) {
        return NULL;
    } else if (payload->data2 != NULL && payload->data2_len == 0) {
        return NULL;
    }

    // prepare the request struct
    rpc_data request = {.data1 = payload->data1,
                        .data2 = payload->data2,
                        .data2_len = payload->data2_len};

    // serialize the request struct and name into payload
    char *request_payload = serialize_payload(&request);
    if (request_payload == NULL) {
        return NULL;
    }

    size_t payload_len = sizeof(rpc_data);

    // send the serialized data to the server
    if (send(cl->socket_fd, request_payload, payload_len, 0) == -1) {
        perror("send");
        return NULL;
    }

    // Send the payload data2 if it exists
    if (request.data2_len > 0 && request.data2 != NULL) {
        size_t data2_len = request.data2_len;

        // Send the serialized payload data2 to the server
        if (send(cl->socket_fd, request.data2, data2_len, 0) == -1) {
            perror("send");
            return NULL;
        }
    }

    // convert index to network byte order and send to the server
    u_int64_t index_network_order = htonl((uint64_t)h->index);

    // send the function index to the server
    if (send(cl->socket_fd, &index_network_order, sizeof(uint32_t), 0) == -1) {
        perror("send");
        return NULL;
    }

    // buffer to hold received data
    unsigned char response_buffer[1024];

    // receive the serialized data from the server
    ssize_t bytes_received =
        recv(cl->socket_fd, response_buffer, sizeof(response_buffer), 0);
    if (bytes_received == -1) {
        perror("recv");
        return NULL;
    }

    // initialize buffer offset to 0
    size_t buffer_offset = 0;

    // create and populate the response struct
    rpc_data *response = malloc(sizeof(rpc_data));
    if (response == NULL) {
        perror("malloc");
        return NULL;
    }

    // deserialize data1 from response_buffer
    uint64_t data1_network;
    memcpy(&data1_network, response_buffer + buffer_offset, sizeof(uint64_t));
    response->data1 = be64toh(data1_network);
    buffer_offset += sizeof(uint64_t);

    // deserialize data2_len from response_buffer
    uint32_t data2_len_network;
    memcpy(&data2_len_network, response_buffer + buffer_offset,
           sizeof(uint32_t));
    response->data2_len = ntohl(data2_len_network);
    buffer_offset += sizeof(uint32_t);

    // read remaining bytes if data2_len non_null
    if (response->data2_len > 0) {
        response->data2 = malloc(response->data2_len + 1);
        if (response->data2 == NULL) {
            perror("malloc");
            free(response);
            return NULL;
        }
        // deserialize data2 from response_buffer
        memcpy(response->data2, response_buffer + buffer_offset,
               response->data2_len);
        ((char *)response->data2)[response->data2_len] = '\0';
    } else {
        response->data2 = NULL;
    }

    if (response->data2 != NULL) {
        if (strcmp((char *)response->data2, "Function failed") == 0) {
            free(response->data2);
            free(response);
            return NULL;
        }
    }

    return response;
}

void rpc_close_client(rpc_client *cl) {
    if (cl == NULL) {
        return;
    }

    if (cl->socket_fd) {
        close(cl->socket_fd);
    }

    free(cl);
}

void rpc_data_free(rpc_data *data) {
    if (data == NULL) {
        return;
    }
    if (data->data2 != NULL) {
        free(data->data2);
    }
    free(data);
}

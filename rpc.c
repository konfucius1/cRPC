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
#define MAX_FUNCTIONS 10

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

// Function to add a registered function to the server
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

static int find_function_index(rpc_server *server, char *function_name) {
    registered_function *functions = server->functions;

    for (int i = 0; i < server->functions_count; i++) {
        if (strcmp(functions[i].name, function_name) == 0) {
            // printf("function found at index: %d\n", i);
            return i;
        }
    }

    return -1;
}

static rpc_data *handle_lookup_request(rpc_server *srv, rpc_data *request,
                                       int socket_fd) {
    char *function_name = malloc(request->data1 + 1);
    rpc_data *response = malloc(sizeof(rpc_data));

    if (recv(socket_fd, function_name, request->data1, 0) == -1) {
        perror("recv");
        free(function_name);
        free(response);
        return NULL;
    }

    function_name[request->data1] = '\0';

    int function_index = find_function_index(srv, function_name);

    if (function_index != -1) {
        response->data1 = function_index;
    } else {
        response->data1 = -1;
    }

    // serialize the response into a byte stream
    size_t response_len = sizeof(int);
    char response_buffer[response_len];
    memcpy(response_buffer, &(response->data1), sizeof(int));

    // send the response byte stream
    if (send(socket_fd, response_buffer, response_len, 0) == -1) {
        perror("send");
    }

    free(function_name);
    return response;
}

static rpc_data *handle_function_invocation(rpc_server *srv, rpc_data *request,
                                            int socket_fd) {
    char *data2 = malloc(request->data2_len);
    if (data2 == NULL) {
        perror("malloc");
        return NULL;
    }

    if (recv(socket_fd, data2, request->data2_len, 0) == -1) {
        perror("recv");
        free(data2);
        return NULL;
    }

    request->data2 = data2;

    int function_index;
    if (recv(socket_fd, &function_index, sizeof(int), 0) == -1) {
        perror("recv");
        return NULL;
    }

    // check if function_index is valid
    if (function_index < 0 || function_index >= srv->functions_count) {
        perror("index");
        return NULL;
    }

    // /* print request_data */
    // printf("Request Data: \n");
    // printf("data1: %d \n", request->data1);
    // printf("data2_len: %ld \n", request->data2_len);
    // if (request->data2 != NULL) {
    //     printf("data2: %d\n", *((char *)request->data2));
    // } else {
    //     printf("data2: NULL\n");
    // }

    rpc_handler *function = &(srv->functions[function_index].handler);

    // printf("Debug: Address of function pointer at index %d is %p\n",
    //        function_index, &(srv->functions[function_index].handler));

    // Check if function is NULL
    if (function == NULL) {
        free(data2);
        return NULL;
    }

    rpc_data *response = (*function)(request);

    rpc_data error_response;
    if ((response == NULL) ||
        (response->data2 == NULL && response->data2_len > 0) ||
        (response->data2 != NULL && response->data2_len == 0)) {
        error_response.data1 = -9999;
        error_response.data2 = strdup("Function failed");
        error_response.data2_len = strlen(error_response.data2) + 1;
        response = &error_response;
    }

    // // rpc_data inconcistency
    // if (response->data2 == NULL && response->data2_len > 0) {
    //     printf("inconsistency issue\n");
    // }

    // if (response->data2 != NULL && response->data2_len == 0) {
    //     printf("data2 not null but data2_len 0\n");
    // }

    // // Debugging output
    // printf("Debug: error_response contents:\n");
    // printf("response->data1: %d\n", response->data1);
    // printf("response->data2_len: %zu\n", response->data2_len);
    // if (response->data2 != NULL) {
    //     printf("data2: %s\n", (char *)response->data2);
    // } else {
    //     printf("data2: NULL\n");
    // }

    // prepare the buffer for serialization
    size_t buffer_size = sizeof(int) + sizeof(size_t) +
                         (response->data2 ? response->data2_len : 0);
    char *buffer = malloc(buffer_size);
    if (!buffer) {
        perror("malloc");
        free(data2);
        free(response);
        return NULL;
    }

    size_t offset = 0;

    // serialize the response struct into the buffer
    memcpy(buffer + offset, &(response->data1), sizeof(int));
    offset += sizeof(int);
    memcpy(buffer + offset, &(response->data2_len), sizeof(size_t));
    offset += sizeof(size_t);

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

        return NULL;
    }

    free(buffer);
    free(data2);

    if (response == &error_response) {
        return NULL;
    } else {
        return response;
    };
}

/**
 * Function runs on the server side, continuously accepts incoming
 * connections, receives requests from clients
 */
void rpc_serve_all(rpc_server *srv) {
    if (srv == NULL) {
        return;
    }

    /* Reference from Beej's guide */
    struct sockaddr_storage their_addr;
    socklen_t addr_size;
    char s[INET6_ADDRSTRLEN];

    while (1) {
        // printf("Waiting for connection...\n");

        // Accept an incoming connection
        addr_size = sizeof(their_addr);
        srv->new_fd =
            accept(srv->socket_fd, (struct sockaddr *)&their_addr, &addr_size);
        if (srv->new_fd == -1) {
            perror("accept");
            continue;
        }

        // printf("Connection accepted.\n");
        // Convert client IP to string
        inet_ntop(their_addr.ss_family,
                  get_in_addr((struct sockaddr *)&their_addr), s, sizeof(s));

        int function_index = 0;
        while (1) {
            rpc_data *response = NULL;

            // printf("Waiting for data...\n");

            // Receive request byte stream
            size_t request_len = sizeof(rpc_data);
            char request_buffer[request_len];
            ssize_t bytes_received =
                recv(srv->new_fd, request_buffer, request_len, 0);
            if (bytes_received <= 0) {
                // testing byte stream
                // if (bytes_received == 0) {
                //     printf("Connection closed by client.\n");
                // } else {
                //     perror("recv");
                // }
                break;
            }

            // Copy the received byte stream into an rpc_data struct
            rpc_data request;
            memcpy(&request, request_buffer, request_len);

            // printf("Data received.\n");
            // printf("data1: %d\n", request.data1);
            // printf("data2_len: %zu\n", request.data2_len);

            if (request.data2_len == 0) {
                // printf("handle for lookup request\n");
                response = handle_lookup_request(srv, &request, srv->new_fd);
                function_index = response->data1;
                // printf("function_index: %d\n", response->data1);
            } else {
                // printf("handle for function call request\n");
                response =
                    handle_function_invocation(srv, &request, srv->new_fd);
                // printf("response: %d\n", response->data1);
            }

            // printf("\tresponse: %d\n", response->data1);

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

rpc_handle *rpc_find(rpc_client *cl, char *name) {
    if (cl == NULL || name == NULL) {
        return NULL;
    }

    // prepare the request struct
    rpc_data request = {.data1 = strlen(name), .data2 = NULL, .data2_len = 0};

    // Prepare the payload byte stream
    size_t payload_len = sizeof(rpc_data) + strlen(name);
    char payload[payload_len];
    memcpy(payload, &request, sizeof(rpc_data));
    memcpy(payload + sizeof(rpc_data), name, strlen(name));

    // Send the payload
    if (send(cl->socket_fd, payload, payload_len, 0) == -1) {
        perror("send");
        return NULL;
    }

    rpc_data response;
    if (recv(cl->socket_fd, &response, sizeof(rpc_data), 0) == -1) {
        perror("receive");
        return NULL;
    }

    if (response.data1 == -1) {
        return NULL;
    } else {
        rpc_handle *function_handle = (rpc_handle *)malloc(sizeof(rpc_handle));
        function_handle->index = response.data1;
        return function_handle;
    }
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

    // /* print request_data */
    // printf("Request Data: \n");
    // printf("data1: %d \n", request.data1);
    // printf("data2_len: %ld \n", request.data2_len);
    // if (request.data2 != NULL) {
    //     printf("data2: %d\n", *((char *)request.data2));
    // } else {
    //     printf("data2: NULL\n");
    // }

    // printf("------------------------\n");

    // prepare the buffer for serialization
    size_t buffer_size = sizeof(rpc_data) + request.data2_len;
    char buffer[buffer_size];
    size_t offset = 0;

    // serialize the request struct into the buffer
    memcpy(buffer, &request, sizeof(rpc_data));
    offset += sizeof(rpc_data);

    // serialize the payload data2 into the buffer if it exists
    if (request.data2_len > 0 && request.data2 != NULL) {
        memcpy(buffer + offset, request.data2, request.data2_len);
        offset += request.data2_len;
    }

    // send the serialized data to the server
    if (send(cl->socket_fd, buffer, buffer_size, 0) == -1) {
        perror("send");
        return NULL;
    }

    // send the function index to the server
    if (send(cl->socket_fd, &(h->index), sizeof(int), 0) == -1) {
        perror("send");
        return NULL;
    }

    // recv
    int response_data1;
    size_t response_data2_len;
    if (recv(cl->socket_fd, &response_data1, sizeof(int), 0) == -1 ||
        recv(cl->socket_fd, &response_data2_len, sizeof(size_t), 0) == -1) {
        perror("recv");
        return NULL;
    }

    // create and populate the response struct
    rpc_data *response = malloc(sizeof(rpc_data));
    if (response == NULL) {
        perror("malloc");
        return NULL;
    }

    response->data1 = response_data1;
    response->data2_len = response_data2_len;
    response->data2 = NULL;

    // read remaining bytes if data2_len non_null
    if (response_data2_len > 0) {
        response->data2 = malloc(response_data2_len);
        if (response->data2 == NULL) {
            perror("malloc");
            free(response);
            return NULL;
        }

        if (recv(cl->socket_fd, response->data2, response_data2_len, 0) == -1) {
            perror("recv");
            free(response->data2);
            free(response);
            return NULL;
        }
    }

    // response->data1 = response_data1;

    // response->data2_len = payload->data2_len;

    // // allocate memory for data2 and copy the content from payload->data2
    // if (payload->data2_len > 0) {
    //     response->data2 = malloc(payload->data2_len);
    //     if (response->data2 == NULL) {
    //         perror("malloc");
    //         free(response);
    //         return NULL;
    //     }
    //     memcpy(response->data2, payload->data2, payload->data2_len);
    // } else {
    //     response->data2 = NULL;
    // }

    // // Debugging output
    // printf("Debug: error_response contents:\n");
    // printf("response->data1: %d\n", response->data1);
    // printf("response->data2_len: %zu\n", response->data2_len);

    if (strcmp((char *)response->data2, "Function failed") == 0) {
        return NULL;
    }

    // check function return data2 and data2_len consistency
    if (response->data2 == NULL && response->data2_len > 0) {
        return NULL;
    }

    return response;
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

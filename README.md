# cRPC (C Remote Procedure Call)

This project implements a Remote Procedure Call (RPC) system in C. The system allows clients to call functions on a remote server as if they were local functions. The server registers functions that can be called by the clients, and the clients can invoke these functions with specified arguments and receive the results.

`rpc.h`: Header file containing the declarations of the RPC structures and functions.\
`client.c`: Client implementation that initializes the RPC client, finds the function handle, and makes RPC calls.\
`server.c`: Server implementation that initializes the RPC server, registers functions, and handles incoming RPC requests.\
`Makefile`: File to compile the project.\
`README.md`: Project documentation (this file).\

## Requirements
`GCC (GNU Compiler Collection)`\
`POSIX-compliant operating system (e.g., Linux, macOS)`

## Installation

Clone the repository:
```sh
git clone https://github.com/yourusername/rpc_project.git
cd rpc_project
```

Compile the project using the provided Makefile:
```sh
make all
```

## Usage
Running the Server
Start the RPC server with the following command:

```
sh
./server -p <port>
Replace <port> with the desired port number.
```

Running the Client
Start the RPC client with the following command:

```sh
Copy code
./client -i <ip-address> -p <port>
Replace <ip-address> with the server's IP address and <port> with the server's port number.
```

## Code Overview
`Client (client.c)`
- Initializes the RPC client.
- Finds the handle for the function "add2".
- Prepares the request data and makes the RPC call.
- Receives and processes the response from the server.

`Server (server.c)`
- Initializes the RPC server.
- Registers functions that can be called by the client.
- Handles incoming requests, deserializes data, invokes the target function, and sends back the response.

## Example Function Registration
In the server, functions can be registered as follows:

```c
rpc_register(srv, "add2", add2_handler);
```

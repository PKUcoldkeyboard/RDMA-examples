# RDMA-examples

RDMA-examples is a repository of practical code examples showcasing the fundamental concepts and usage of RDMA (Remote Direct Memory Access) technology.

## Server Workflow
- Initialize RDMA resources.
- Wait for client connection.
- Allocate and pin server buffer.
- Accept client connection.
- Send information about the local server buffer to the client.
- Wait for disconnection.

## Client Workflow
- Initialize RDMA resources.
- Connect to the server.
- Exchange information about the server buffer by sending/receiving.
- Perform RDMA write from the local buffer to the server buffer.
- Perform RDMA read to read the content of the server buffer into a second local buffer.
- Compare the content of the first and second local buffers for matching.
- Disconnect.
  
##Usage
- To start the server, use the following command:

```
bin/server -a <host> -p <port>
```

- To start the client, use the following command:

```
bin/client -a <host> -p <port> -s <message>
```

Replace <host> with the desired host address and <port> with the desired port number. For the client, <message> is the message to be sent to the server. if <host> and <port> are not set, the program will use default values.

Please note that RDMA-examples assumes that RDMA resources are properly set up and configured on the system.

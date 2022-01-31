// MT echo server for testing client

#include <stdio.h>
#include <stdlib.h>
#include <string.h>			//strlen
#include <stdlib.h>			//strlen
#include <sys/socket.h>
#include <arpa/inet.h>		//inet_addr
#include <unistd.h>			//write
#include <pthread.h> 		//for threading , link with lpthread
#include "interface.h"

// Cpp headers
#include <iostream>
using namespace std;

/* Macros */
#define CREATE 	('\x01')
#define DELETE 	('\x02')
#define JOIN	('\x03')
#define LIST	('\x04')

typedef uint32_t u32;

#define MAX_NAME 	(256)
#define MAX_MEMBERS (20)
#define MAX_ROOM	(20)

typedef struct {
	char room_name[MAX_NAME];
	int port_num;
	int num_members;
	int member_socks[MAX_MEMBERS];
} room;

room room_table[MAX_ROOM];
int num_rooms = 0;

pthread_mutex_t room_table_mutex;

/* Simple server for testing, we will manually compose responses and test
	client buffer composition
	client response routing and parsing
	client's ability to connect to slave port
	client's ability to receive responses
*/


// forward declaration
void* connection_handler(void*);
void* chat_handler(void*);

void int_to_bytearray(char* ba, int* i) {
	/* (!) test on c9 */
	memcpy(ba, i, 4);
}

int room_exists(char* name) {
	for (int i = 0; i < num_rooms; i++) {
		if (strcmp(room_table[i].room_name, name) == 0) {
			return 1;
		}
	}
	return 0;
}

int main(int argc , char *argv[]) {

    // Get sock
    if (argc != 2) {
        cout << "Error, input socket number\n";
        exit(1);
    }

    int sock_in = atoi(argv[1]);

	int socket_desc , client_sock , c , *new_sock;
	struct sockaddr_in server , client;
	
	//Create socket
	socket_desc = socket(AF_INET, SOCK_STREAM, 0);
	if (socket_desc == -1)
		cout << "Could not create socket\n";
	cout << "Socket created\n";
	
	//Prepare the sockaddr_in structure
	server.sin_family = AF_INET;
	server.sin_addr.s_addr = INADDR_ANY;
	server.sin_port = htons(sock_in);
	
	//Bind
	if (bind(socket_desc,(struct sockaddr *)&server , sizeof(server)) < 0) {
		perror("bind failed. Error");
		return 1;
	}
	puts("bind done");
	
	//Listen
	listen(socket_desc, 3);
	
	//Accept and incoming connection
	puts("Waiting for incoming connections...");
	c = sizeof(struct sockaddr_in);

	while((client_sock = accept(socket_desc, (struct sockaddr *)&client, (socklen_t*)&c))) {
		puts("Connection accepted");
		
		pthread_t sniffer_thread;
		// new_sock = malloc(1);
        new_sock = new int;		// new socket_t (!)
		*new_sock = client_sock;
		
		if(pthread_create(&sniffer_thread, NULL, connection_handler, (void*) new_sock) < 0) {
			perror("could not create thread");
			return 1;
		}
		
		//Now join the thread , so that we dont terminate before the thread
		//pthread_join(sniffer_thread, NULL);
		puts("Handler assigned");
	}
	
	if (client_sock < 0) {
		perror("accept failed");
		return 1;
	}
	
	return 0;
}

int handle_CREATE(int sock, char* name) {
	char resp[MAX_DATA];

	// 1. Check if data struct has space
	// 2. Check if room already in data struct
	pthread_mutex_lock(&room_table_mutex);
	if (num_rooms >= MAX_ROOM || room_exists(name)) {
		resp[0] = (char) FAILURE_INVALID;
	} else {
		// 3. Find available port to set aside for chatroom
		

		// 4. Save to data struct and resp to client
		
	}

	write(sock, resp, MAX_DATA);
	pthread_mutex_unlock(&room_table_mutex);

	return 1;
}

void* chat_handler(void* sockfd) {
	int sock = *(int*) sockfd;
	int recv_size;
	char client_message[MAX_DATA];

	// recv
	/* SHOULD ITER THROUGH ALL ON PORT AND FORWARD (!)(!)(*)*/
	while((recv_size = recv(sock, client_message, MAX_DATA, 0)) > 0) {
		client_message[strlen(client_message)] = '\0';

		// cout << "> " << client_message << '\0';
		fprintf(stdout, "> %s\n", client_message);

		// echo back(!)
		write(sock, client_message, strlen(client_message));
	}

	if(recv_size == 0) {
		cout << "Chat client disco\n";
		fflush(stdout);
	} else if (recv_size == -1) {
		perror("recv failed");
	}

	free(sockfd);
	return 0;
}

int handle_JOIN(int master_sock) {
	// (!) goes in CREATE, JOIN will simply fetch the proper port
	// IMPL mutex on fetching proper port or account numbers
	// 1. Get new port for chatroom (later we will check/update)
	// 1* Fetch port from datastruct
	int chat_sock;
	if ((chat_sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		cout << "Failure on chatroom socket init\n";
		return -1;
	}
	cout << "Chatroom sock init successful\n";
	
	// init serv struct
	struct sockaddr_in serv_addr;
	bzero((char*) &serv_addr, sizeof(serv_addr));
	serv_addr.sin_addr.s_addr = INADDR_ANY;
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = 0;	// auto

	// try bind auto-selected socket
	if (bind(chat_sock, (struct sockaddr*) &serv_addr, sizeof(serv_addr)) < 0) {
		if (errno == EADDRINUSE) {
			cout << "Failure, chatroom port not available\n";
			return -1;
		} else {
			cout << "Failure on chatroom port bind\n";
			return -1;
		}
	}
	cout << "Chatroom port bind done\n";

	// sockname error
	socklen_t len = sizeof(serv_addr);
	if (getsockname(chat_sock, (struct sockaddr*) &serv_addr, &len) == -1) {
		perror("getsockname");
		return -1;
	}

	// chat_port should be network byte order serialized
	int chat_port = ntohs(serv_addr.sin_port);
	// int chat_port = serv_addr.sin_port;
	// check num_members, dbg=42 (!)
	
	// 2. Byte order serialize param(s)
	int tmembers = (int) htonl((u32) 42);
	chat_port = (int) htonl((u32) chat_port);

	// 3. fill in resp buffer { 1B STATUS | 4B N_MEMBER | 4B PORT }
	char resp[MAX_DATA];
	resp[0] = (char) SUCCESS;
	int_to_bytearray(resp + 1, &tmembers);
	int_to_bytearray(resp + 5, &chat_port);

	// 4. Write resp to client and listen for incoming
	write(master_sock, resp, MAX_DATA);
	
	cout << "Waiting for chatroom connection\n";
	listen(chat_sock, 3);

	// 5. On connection, spawn a new thread to handle chatroom
	int size = sizeof(struct sockaddr_in);
	int client_sock;
	int* new_sock;
	struct sockaddr_in client;
	while((client_sock = accept(chat_sock, (struct sockaddr*) &client, (socklen_t*) &size))) {
		cout << "Chatroom connection accepted\n";

		pthread_t s_thread;
		new_sock = new int;
		*new_sock = client_sock;

		if(pthread_create(&s_thread, NULL, chat_handler, (void*) new_sock) < 0) {
			perror("could not create thread");
			return 1;
		}
		
		// Join thread (!)
		// pthread_join(s_thread, NULL);

		cout << "Handler assigned\n";

	}

	return 1;
}

/*
 * This will handle connection for each client
 * */
void *connection_handler(void *socket_desc) {
	//Get the socket descriptor
	int sock = *(int*)socket_desc;

	// free(socket_desc); exit(1);

	int recv_size;
	char client_message[MAX_DATA], resp[MAX_DATA];
	char cmd;

	//Receive a message from client
	while((recv_size = recv(sock, client_message, MAX_DATA , 0)) > 0) {
		client_message[strlen(client_message)] = '\0';
		cmd = client_message[0];
		if (cmd == CREATE) {
			cout << "CREATE|" << client_message+1 << '\n';
			resp[0] = (char) SUCCESS;
		} else if (cmd == DELETE) {
			cout << "DELETE|" << client_message+1 << '\n';
			resp[0] = (char) SUCCESS;
		} else if (cmd == JOIN) {
			cout << "JOIN|" << client_message +1 << '\n';
			
			/* RAW testing (!)
			char members_port[8];
			int tmembers = (int) htonl((u32) 42);
			int tport = (int) htonl((u32) 777);
			int_to_bytearray(members_port, &tmembers);
			int_to_bytearray(members_port + 4, &tport);
			resp[0] = (char) SUCCESS;
			memcpy(resp+1, members_port, 8);
			*/
			if (handle_JOIN(sock) < 0) {
				cout << "Failure on handle join\n";
				exit(1);
			}
			return 0;

		} else if (cmd == LIST) {
			cout << "LIST\n";
			resp[0] = (char) SUCCESS;
			const char* lstring = "room1, room2, room3, room4";
			int len = strlen(lstring) + 1;
			strlcpy(resp+1, lstring, len);
		}
		
		
		else {
			cout << "$ <" << client_message << ">\n";
			cout << "L <" << strlen(client_message) << ">\n";
			cout << "<<< echoing buffer\n";
			write(sock, client_message , strlen(client_message));
		}
		
		// Send response buffer
		write(sock, resp, MAX_DATA);
	}
	
	if(recv_size == 0) {
		puts("Client disconnected");
		fflush(stdout);
	} else if(recv_size == -1) {
		perror("recv failed");
	}
		
	//Free the socket pointer
	free(socket_desc);
	
	return 0;
}


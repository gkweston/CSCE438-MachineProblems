#include <netdb.h>
#include <netinet/in.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include "interface.h"

// cpp
#include <iostream>
using namespace std;

/* Macros */
#define CREATE 	('\x01')
#define DELETE 	('\x02')
#define JOIN	('\x03')
#define LIST	('\x04')

/* Types */
typedef uint8_t u8;
typedef uint32_t u32;

/* Forwards */
int connect_to(const char *host, const int port);
struct Reply process_command(const int sockfd, char* command);
void process_chatmode(const char* host, const int port);

/* Globals */
u8 GLOBAL_UID;	// assigned by the server after a JOIN command

int main(int argc, char** argv) 
{
	if (argc != 3) {
		fprintf(stderr,
				"usage: enter host address and port number\n");
		printf("Please use tclient 127.0.0.1 8888\n");
		exit(1);
	}

    display_title();
    
	while (1) {
	
		int sockfd = connect_to(argv[1], atoi(argv[2]));
    
		char command[MAX_DATA];
        get_command(command, MAX_DATA);

		struct Reply reply = process_command(sockfd, command);
		display_reply(command, reply);
		
		touppercase(command, strlen(command) - 1);
		if (strncmp(command, "JOIN", 4) == 0 && reply.status == SUCCESS) {
			printf("Now you are in the chatmode\n");
			process_chatmode(argv[1], reply.port);
		}
	
		close(sockfd);
    }

    return 0;
}

/*
 * Connect to the server using given host and port information
 *
 * @parameter host    host address given by command line argument
 * @parameter port    port given by command line argument
 * 
 * @return socket fildescriptor
 */
int connect_to(const char *host, const int port)
{
	int sock;
	struct sockaddr_in server;

	// Create TCP socket
	sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock == -1) {
		perror("Failed on socket init\n");
	}

	// Set up server info
	server.sin_addr.s_addr = inet_addr(host);
	server.sin_family = AF_INET;
	server.sin_port = htons(port);

	// Issue connect, propogate error upwards w/ -1
	if (connect(sock, (struct sockaddr*)&server, sizeof(server)) < 0) {
		perror("Failed on connect\n");
		return -1;
	}

	return sock;
}

int space_idx(char* buf) {
	// get the index of our strtok delimiter ' '
	for (int i = 0; i < strlen(buf) - 1; i++) {
		if (buf[i] == ' ') {
			return i;
		}
	}
	return -1;
}

char set_command_buf(char* buf, char* command) {
	/*
	Command buffer is {1B command||NB Parameter}
	populate this buffer, then return the current
	command so we can reference it in the resp
	*/

	/*
		For all except LIST check if user input
		"COMMAND" or "COMMAND " which would result
		in segfault by way of strtok
	*/
	if (strncmp(command, "LIST", 4) != 0) {
		int idx = space_idx(command);
		int c_len = strlen(command);
		if (idx == -1 || idx == (c_len - 1)) {
			cout << "CREATE, DELETE, JOIN must take a parameter\n";
			get_command(command, MAX_DATA);
			return set_command_buf(buf, command);
		}
	}

	// Get command type
	char* cmd = strtok(command, " ");
	char* name;
	char cmd_code;
	int len;

	if (strncmp(cmd, "CREATE", 6) == 0) {
		cmd_code = CREATE;
	} else if (strncmp(cmd, "DELETE", 6) == 0) {
		cmd_code = DELETE;
	} else if (strncmp(cmd, "JOIN", 4) == 0) {
		cmd_code = JOIN;
	} else if (strncmp(cmd, "LIST", 4) == 0) {
		buf[0] = LIST;
		buf[1] = '\0';
		return LIST;
	} else {
		printf("Unknown command to parse\n");
		return -1;
	}

	// Get command params
	name = strtok(NULL, " ");

	// Compose buffer
	buf[0] = cmd_code;
	len = strlen(name);
	memcpy(buf + 1, name, len);
	buf[len + 1] = '\0';
		
	return cmd_code;
}

void bytearray_to_int(int* i, char* ba) {
	// Verify this call on C9, else bitshift the value (*)(!)
	memcpy(i, ba, 4);
}

/* 
 * Send an input command to the server and return the result
 *
 * @parameter sockfd   socket file descriptor to commnunicate
 *                     with the server
 * @parameter command  command will be sent to the server
 *
 * @return    Reply    
 */
struct Reply process_command(const int sockfd, char* command)
{
	char cmd_buf[MAX_DATA];
	char cmd;
	
	if ((cmd = set_command_buf(cmd_buf, command)) == -1) {
		printf("Failed on setting command buf\n");
		exit(1);
	}

	// * Send
	if (send(sockfd, cmd_buf, strlen(cmd_buf) + 1, 0) < 0) {
		printf("Failed on send\n");
		return (struct Reply) { FAILURE_UNKNOWN, };
	}

	// * Recv
	int recv_size;
	char reply_buf[MAX_DATA];
	if ((recv_size = recv(sockfd, reply_buf, MAX_DATA, 0)) < 0) {
		printf("Failed on recv\n");
		return (struct Reply) { FAILURE_UNKNOWN, };
	}

	// Enum key:
	// SUCCESS					=\x00
    // FAILURE_ALREADY_EXISTS	=\x01
    // FAILURE_NOT_EXISTS		=\x02
    // FAILURE_INVALID			=\x03
    // FAILURE_UNKNOWN			=\x04
	
	// Expect packet = {1B STATUS||<=255B MSG }
	struct Reply repl;
	repl.status = (enum Status)reply_buf[0];

	// Return error
	if (repl.status != SUCCESS) 
		return repl;

	// Parse responses into repl struct
	switch (cmd) {
		case CREATE:
			break;

		case DELETE:
			printf("%s", reply_buf + 1);
			break;

		case JOIN:
			// Expect packet = {1B STATUS||1B UID||4B N_MEMBER||4B PORT}
			// 4B integers are serialized network byteorder (Big-Endian)
			
			// Extract & place parameters in ints, network byte order (!) test on C9 (*)
			GLOBAL_UID = *(reply_buf + 1);
			int n_members_nb, port_nb;
			bytearray_to_int(&n_members_nb, reply_buf + 2);
			bytearray_to_int(&port_nb, reply_buf + 6);

			repl.num_member = (int) ntohl((u32) n_members_nb);
			repl.port = (int) ntohl((u32) port_nb);
			break;

		case LIST:
			strcpy(repl.list_room, reply_buf+1);
			break;

		default:
			printf("Failure, unknown command recv'd\n");
	}

	return repl;
}

void* server_response_handler(void* _sock) {

	int sock = *(int*)_sock;
	char buf[MAX_DATA];
	int size_recv;

	while ((size_recv = recv(sock, buf, MAX_DATA, 0)) > 0) {
		buf[size_recv] = '\0';
		if (buf[0]) {
			display_message(buf);
		}
		cout << '\n';
	}
	cout << "Server disco!\n";

	return 0;
}

/* 
 * Get into the chat mode
 * 
 * @parameter host     host address
 * @parameter port     port
 */
void process_chatmode(const char* host, const int port)
{

	int chat_sockfd = connect_to(host, port);

	// * send uid to the server so it can sync
	char uid_send = (char)GLOBAL_UID;
	if (send(chat_sockfd, &GLOBAL_UID, 1, 0) < 0) {
		perror("UID send failure");
	}

	char user_in[MAX_DATA - 1];
	char msg[MAX_DATA];
	char resp[MAX_DATA];
	int recv_size;

	pthread_t s_thread;
	pthread_create(&s_thread, NULL, server_response_handler, &chat_sockfd);

	while(true) {
		// * Get message from client
		get_message(user_in, MAX_DATA);
		
		// * Prepend UID to chat message = {1B UID||NB PAYLOAD}
		msg[0] = (char) GLOBAL_UID;
		strcpy(msg + 1, user_in);

		// * Send on sock
		if (send(chat_sockfd, msg, MAX_DATA, 0) < 0) {
			puts("Failure on send");
			exit(1);
		}

		display_message(resp);
		cout << '\n';
		bzero(resp, strlen(resp));
	}

}


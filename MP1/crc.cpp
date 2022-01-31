/*
Notes:
- 1st msg sent will be a comman msg, LIST, JOIN, ...
- All other msg will be char*
*/

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

/*
 * TODO: IMPLEMENT BELOW THREE FUNCTIONS
 */
int connect_to(const char *host, const int port);
struct Reply process_command(const int sockfd, char* command);
void process_chatmode(const char* host, const int port);

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
		if (strncmp(command, "JOIN", 4) == 0) {
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
	// ------------------------------------------------------------
	// GUIDE :
	// In this function, you are suppose to connect to the server.
	// After connection is established, you are ready to send or
	// receive the message to/from the server.
	// 
	// Finally, you should return the socket fildescriptor
	// so that other functions such as "process_command" can use it
	// ------------------------------------------------------------

	int sock;
	struct sockaddr_in server;

	// Create TCP socket
	sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock == -1) {
		printf("Failed on socket init\n");
	}
	printf("Socket init success\n");

	// Set up server info
	// server.sin_addr.s_addr = inet_addr("127.0.0.1");
	server.sin_addr.s_addr = inet_addr(host); //(!)
	server.sin_family = AF_INET;
	server.sin_port = htons(port);

	// Issue connect, propogate error upwards w/ -1
	if (connect(sock, (struct sockaddr*)&server, sizeof(server)) < 0) {
		printf("Failed on connect\n");
		return -1;
	}
	printf("Connect success\n");

	return sock;
}

char set_command_buf(char* buf, char* command) {
	/*
	Command buffer is { 1B command | NB Parameter}
	populate this buffer, then return the current
	command so we can reference it in the resp
	*/
	
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

typedef uint32_t u32;

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
	// ------------------------------------------------------------
	// GUIDE 1:
	// In this function, you are supposed to parse a given command
	// and create your own message in order to communicate with
	// the server. Surely, you can use the input command without
	// any changes if your server understand it. The given command
    // will be one of the followings:
	//
	// CREATE <name>
	// DELETE <name>
	// JOIN <name>
    // LIST
	//
	// -  "<name>" is a chatroom name that you want to create, delete,
	// or join.
	// 
	// - CREATE/DELETE/JOIN and "<name>" are separated by one space.
	// ------------------------------------------------------------
	char cmd_buf[MAX_DATA];
	char cmd;
	
	if ((cmd = set_command_buf(cmd_buf, command)) == -1) {
		printf("Failed on setting command buf\n");
		exit(1);
	}

	// ------------------------------------------------------------
	// GUIDE 2:
	// After you create the message, you need to send it to the
	// server and receive a result from the server.
	// ------------------------------------------------------------
	
	// Send
	if (send(sockfd, cmd_buf, strlen(cmd_buf), 0) < 0) {
		printf("Failed on send\n");
		return (struct Reply) { FAILURE_UNKNOWN, };
	}
	// Recv
	int recv_size;
	char reply_buf[MAX_DATA];
	if ((recv_size = recv(sockfd, reply_buf, MAX_DATA, 0)) < 0) {
		printf("Failed on recv\n");
		return (struct Reply) { FAILURE_UNKNOWN, };
	}

	cout << "Send & recv successful\n"; //(!)

	// ------------------------------------------------------------
	// GUIDE 3:
	// Then, you should create a variable of Reply structure
	// provided by the interface and initialize it according to
	// the result.
	//
	// For example, if a given command is "JOIN room1"
	// and the server successfully created the chatroom,
	// the server will reply a message including information about
	// success/failure, the number of members and port number.
	// By using this information, you should set the Reply variable.
	// the variable will be set as following:
	//
	// Reply reply;
	// reply.status = SUCCESS;
	// reply.num_member = number;
	// reply.port = port;
	// 
	// "number" and "port" variables are just an integer variable
	// and can be initialized using the message fomr the server.
	//
	// For another example, if a given command is "CREATE room1"
	// and the server failed to create the chatroom becuase it
	// already exists, the Reply varible will be set as following:
	//
	// Reply reply;
	// reply.status = FAILURE_ALREADY_EXISTS;
    // 
    // For the "LIST" command,
    // You are suppose to copy the list of chatroom to the list_room
    // variable. Each room name should be seperated by comma ','.
    // For example, if given command is "LIST", the Reply variable
    // will be set as following.
    //
    // Reply reply;
    // reply.status = SUCCESS;
    // strcpy(reply.list_room, list);
    // 
    // "list" is a string that contains a list of chat rooms such 
    // as "r1,r2,r3,"
	// ------------------------------------------------------------

	// Notes:
	/* #define CREATE 	('\x01')
		-> Return result to user
	   #define DELETE 	('\x02')
	   	-> Return result, with a warning to chatroom users
	   #define JOIN		('\x03')
	   	-> Return result, num_members, port
	   #define LIST		('\x04')
	   	-> Return result, buf of LIST
		-> Expect LIST is nullterm   */

	// SUCCESS					=\x00
    // FAILURE_ALREADY_EXISTS	=\x01
    // FAILURE_NOT_EXISTS		=\x02
    // FAILURE_INVALID			=\x03
    // FAILURE_UNKNOWN			=\x04
	
	// Expect packet = { 1B STATUS | <=256B MSG }
	struct Reply repl;
	repl.status = (enum Status)reply_buf[0];

	if (cmd == CREATE && repl.status == SUCCESS) {
		// Expect packet = { 1B Status }
		printf("Room created!\n");
	} else if (cmd == DELETE && repl.status == SUCCESS) {
		// Expect packet = { 1B Status | <256B Warning }
		printf("%s", reply_buf + 1);
	} else if (cmd == JOIN) {
		// Expect packet = { 1B Status | 4B N Members | 4B Port }
		// 4B integers are serialized network byteorder (Big-Endian)
		
		// Place parameters in ints, network byte order (!) test on C9 (*)
		int n_members_nb, port_nb;
		bytearray_to_int(&n_members_nb, reply_buf + 1);
		bytearray_to_int(&port_nb, reply_buf + 5);

		repl.num_member = (int) ntohl((u32) n_members_nb);
		repl.port = (int) ntohl((u32) port_nb);

		cout << "Join num_member: " << repl.num_member << '\n';
		cout << "Join port: " << repl.port << '\n';

	} else if (cmd == LIST) {
		strcpy(repl.list_room, reply_buf+1);
	} else printf("Failure, unknown command recv'd\n");
	
	return repl;
}

/* 
 * Get into the chat mode
 * 
 * @parameter host     host address
 * @parameter port     port
 */
void process_chatmode(const char* host, const int port)
{

	/* In this debug step, wait for response before taking more input (!) */

	// ------------------------------------------------------------
	// GUIDE 1:
	// In order to join the chatroom, you are supposed to connect
	// to the server using host and port.
	// You may re-use the function "connect_to".
	// ------------------------------------------------------------
	cout << "Chatroom client issuing connection\n";
	int chat_sockfd = connect_to(host, port);

	// ------------------------------------------------------------
	// GUIDE 2:
	// Once the client have been connected to the server, we need
	// to get a message from the user and send it to server.
	// At the same time, the client should wait for a message from
	// the server.
	// ------------------------------------------------------------
	// debug step (!)
	// char msgbuf[MAX_DATA];
	// get_message(msgbuf, MAX_DATA);
	char msg[MAX_DATA];
	char resp[MAX_DATA];
	int recv_size;

	while(true) {
		/* MULTIPLEX between these two (!)(!)(*)(*)*/
		// Get message from client
		printf("$ ");
		get_message(msg, MAX_DATA);

		// Send on sock
		if (send(chat_sockfd, msg, MAX_DATA, 0) < 0) {
			puts("Failure on send");
			exit(1);
		}

		// Wait for reply from server (!) dbg
		if ((recv_size = recv(chat_sockfd, resp, MAX_DATA, 0)) < 0) {
			puts("Failure on recv");
			exit(1);
		}
		display_message(resp);
		cout << '\n';
		bzero(resp, strlen(resp));
	}
	
    // ------------------------------------------------------------
    // IMPORTANT NOTICE:
    // 1. To get a message from a user, you should use a function
    // "void get_message(char*, int);" in the interface.h file
    // 
    // 2. To print the messages from other members, you should use
    // the function "void display_message(char*)" in the interface.h
    //
    // 3. Once a user entered to one of chatrooms, there is no way
    //    to command mode where the user  enter other commands
    //    such as CREATE,DELETE,LIST.
    //    Don't have to worry about this situation, and you can 
    //    terminate the client program by pressing CTRL-C (SIGINT)
	// ------------------------------------------------------------
}


#include <sys/socket.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string>
#include <vector>
#include <iostream>
#include "interface.h"

using namespace std;

/* Macros */
#define CREATE 	('\x01')
#define DELETE 	('\x02')
#define JOIN	('\x03')
#define LIST	('\x04')

#define MAX_ROOMS   (10)
#define MAX_MEMBERS (20)

/* Types */
typedef uint8_t u8;
typedef uint32_t u32;

struct room {
private:
    u8 uid_counter;

public:
    int n_members;
    int port;
    int sock;
    string name;
    vector<u8> client_UIDs;     // We enforce <256 members so a u8 is large enough
    vector<int> client_socks;

    room(string _name, int _port) {
        name = _name;
        port = _port;
        uid_counter = 1;        // start at 1 so we don't shadow '\0'
    }
    
    void add_client(int client_sock, u8 client_uid) {
        // Add to client_socks, client_UIDs, return UID
        ++n_members;
        sock = client_sock;
        client_socks.push_back(client_sock);
        client_UIDs.push_back(client_uid);
    }

    char get_next_uid() {
        return (char) uid_counter++;
    }
};

/* Globals */

// Our "database"
vector<room*> GLOBAL_ROOM_TABLE;

// Rather than auto port selecting, we'll increment this start port
// and map it to room clients
int GLOBAL_START_PORT = 8090;

/* Forwards */
void* connection_handler(void* _sock);

int main(int argc, char** argv) {
    // * parse user input for sock
    if (argc != 2) {
        cout << "Error, input socket number\n";
        exit(1);
    }
    
    // * init master sock
    int sock_in = atoi(argv[1]);
    int socket_desc;
	if ((socket_desc = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		perror("Could not create socket");
    }

    // * make the server
    struct sockaddr_in server, client;
    bzero((char*) &server, sizeof(server));

	server.sin_family = AF_INET;
    server.sin_addr.s_addr = inet_addr("127.0.0.1");
	server.sin_port = htons(sock_in);

    // * bind master socket
    if (bind(socket_desc,(struct sockaddr *)&server , sizeof(server)) < 0) {
		perror("bind failed. Error");
		return 1;
	}

    // * listen for incoming
    listen(socket_desc, 15);

    // * accept incoming
    int client_sock;
    int* sock_cp;
    socklen_t csize = sizeof(struct sockaddr);
    while((client_sock = accept(socket_desc, (struct sockaddr*) &client, &csize))) {
        if (client_sock < 0) {
            perror("Failure on accept");
        }

        // * dispatch connection_handler thread
        sock_cp = new int;//(!) int
        *sock_cp = client_sock;

        pthread_t s_thread;
        if (pthread_create(&s_thread, NULL, connection_handler, (void*) sock_cp)) {
            perror("Failure on connection_handler_thread creation");
            exit(1);
        }
    }
    close(socket_desc);
    return 0;
}

// Handle chatroom forwards
void* msg_forward_handler(void* _room) {
    
    /*
        Expect MSG = {1B UID||NB PAYLOAD}
        compare UID so we don't echo to the sender
    */

    room* chatroom;
    char buf[MAX_DATA];
    int room_sock, recv_size, n_members;
    u8 recv_uid;

    // * cast room from void*
    chatroom = (room*) _room;
    room_sock = chatroom->sock;

    // * recv data on sock
    while((recv_size = recv(room_sock, buf, MAX_DATA, 0)) > 0) {
        if (recv_size < 0) {
            perror("Failure on chatroom recv...");
            exit(1);
        }
        buf[strlen(buf)] = '\0';

        // * extract UID from message
        // * iterate through room->client_socks and forward messages
        recv_uid = buf[0];
        for (int i = 0; i < chatroom->n_members; i++) {
            
            // (!) ping to test server connection
            // ping(chatroom->client_socks[i], 3, 1);//(!!)
            
            if (recv_uid != chatroom->client_UIDs[i]) {
                if (send(chatroom->client_socks[i], buf, sizeof(buf) + 1, 0) < 0) {
                   perror("Failure msg forward");
                }
            }
        }
    }
    return 0;
}

void ping(int sock, int n, int slp) {
    // ping on sock, n times, sleep for slp secs
    for (int j = 0; j < n; j++) {//(!!)
        char ping[] = "PING";
        cout << "Ping " << j << " on sock " << sock << '\n';//(!!)
        if (send(sock, ping, sizeof(ping) + 1, 0) < 0) {
            perror("Failure ping");//(!)
        }
        sleep(slp);
    }
}

// Create a chatroom
void* room_handler(void* _room) {
    room* chatroom;
    int port, sock, client_sock;
    string name;
    char buf[MAX_DATA], resp[MAX_DATA];
    
    // * cast room and pull information from void* room
    chatroom = (room*) _room;
    port = chatroom->port;
    name = chatroom->name;

    // * init sock and server to monitor room->port
    if ( (sock = socket(AF_INET, SOCK_STREAM, 0)) < 0 ) {
        perror("Failure on sock init");
        exit(1);
    }
    // This is what we were missing in the previous "version" (*)
    // enable local address reuse!
    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in serv_addr;
	bzero((char*) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port); //(*) must take network byte order!
    serv_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
	
    // * bind sock to server
    if (bind(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == -1) {
        perror("Failure on chatroom sock binding");
        exit(1);
    }

    // * listen
    if (listen(sock, 15) != 0) {
        perror("Failure on bind");
    }

    // * accept and add client to room->client_socks
    struct sockaddr_in client_addr;
    bzero((char*) &client_addr, sizeof(client_addr));
    socklen_t size = sizeof(client_addr);
    while((client_sock = accept(sock, (struct sockaddr*)&client_addr, &size))) {
        if (client_sock < 0) {
            perror("Error on accept");
        }

        // Take the client UID as the 1st msg for sync
        u8 client_uid;
        if (recv(client_sock, &client_uid, 1, 0) < 0) {
            perror("Error on recv UID");
        }
        chatroom->add_client(client_sock, client_uid);
        
        // * dispatch thread to handle room
        pthread_t s_thread;
        if(pthread_create(&s_thread, NULL, msg_forward_handler, chatroom)) {
            perror("Failure on msg_forward_handler thread init");
            exit(1);
        }
    }

    return 0;
}

room* get_room(string name) {
    for (int i = 0; i < GLOBAL_ROOM_TABLE.size(); i++) {
        if (GLOBAL_ROOM_TABLE[i]->name == name) {
            return GLOBAL_ROOM_TABLE[i];
        }
    }
    return nullptr;
}

// Check if room exists, add to table and start thread
char CREATE_resp(string name) {
    room* chatroom;

    // * check if room exists
    if (get_room(name) != nullptr) {
        return (char) FAILURE_ALREADY_EXISTS;
    }

    // * check if adding room excedes max
    if (GLOBAL_ROOM_TABLE.size() >= MAX_ROOMS) {
        return (char) FAILURE_INVALID;
    }

    // * create new room and add to GLOBAL_ROOM_TABLE
    chatroom = new room(name, GLOBAL_START_PORT++);
    GLOBAL_ROOM_TABLE.push_back(chatroom);

    // * dispatch thread to monitor this socket
    pthread_t s_thread;
    if(pthread_create(&s_thread, NULL, room_handler, chatroom) != 0) {
        perror("Thread creation failed");
        exit(1);
    }

    // * return status code
    return (char) SUCCESS;
}

// Connecton client to room if valid
string JOIN_resp(string name) {
    // returns {1B STATUS||1B UID||4B N_MEMBERS||4B PORT}
    // serialized network byte-order
    room* chatroom;
    char resp[10];
    int room_port, n_members;

    // * check if room in GLOBAL_ROOM_TABLE
    if((chatroom = get_room(name)) == nullptr) {
        resp[0] = (char) FAILURE_NOT_EXISTS;
        memset(resp + 1, '\xff', 9);
        return string(resp, 10);
    }
    
    // * check if room has space
    if(chatroom->n_members >= MAX_MEMBERS) {
        resp[0] = (char) FAILURE_INVALID;
        memset(resp + 1, '\xff', 9);
        return string(resp, 10);
    }

    // * compose resp {1B STATUS||1B UID||4B N_MEMBERS||4B PORT}
    resp[0] = (char) SUCCESS;
    resp[1] = chatroom->get_next_uid();

    room_port = htonl((u32) chatroom->port);
    n_members = htonl((u32) chatroom->n_members);
    
    /* ------------------------------------------------- */
    //(!) verify this is portable to C9 (!)
    memcpy(resp + 2, &n_members, 4);//(!)
    memcpy(resp + 6, &room_port, 4);//(!)
    /* ------------------------------------------------- */

    return string(resp, 10);
}

// List query handler
string LIST_resp() {
    string list_str = "";
    int n_rooms = GLOBAL_ROOM_TABLE.size();

    if (n_rooms == 0) {
        return ((char) SUCCESS) + string("NONE");
    }

    // * get room names from GLOBAL_ROOM_TABLE and compose resp string
    for (int i = 0; i < n_rooms - 1; i++) {
        list_str += (GLOBAL_ROOM_TABLE[i]->name + ", ");
    }
    list_str += GLOBAL_ROOM_TABLE[n_rooms - 1]->name;

    // * return resp string
    return ((char) SUCCESS) + list_str;
}

// Delete query handler
char DELETE_resp(string name) {
    room* chatroom = nullptr;
    char warning[] = "Delete request for room received\nClosing...\n";
    int idx, client_sock;

    if (name == "") {
        return (char) FAILURE_INVALID;
    }

    // * check if name in GLOBAL_ROOM_TABLE, save its index
    for (idx = 0; idx < GLOBAL_ROOM_TABLE.size(); idx++) {
        if (GLOBAL_ROOM_TABLE[idx]->name == name) {
            chatroom = GLOBAL_ROOM_TABLE[idx];
            break;
        }
    }
    if (chatroom == nullptr)
        return (char) FAILURE_NOT_EXISTS;

    // * send room_close warning to clients
    for (int i = 0; i < chatroom->n_members; i++) {
        client_sock = chatroom->client_socks[i];
        if (send(client_sock, warning, strlen(warning) + 1, 0) < 0) {
            perror("Failure delete warning send");
            exit(1);
        }
        // * close room sockets
        close(client_sock);
    }

    // * free room pointer and remove elem in GLOBAL_ROOM_TABLE
    delete chatroom;
    GLOBAL_ROOM_TABLE.erase(GLOBAL_ROOM_TABLE.begin() + idx);

    // * return status code
    return (char) SUCCESS;
}

// Route command based on string parsing
// (!) convert return(string) to char[] for speed (!)
void* connection_handler(void* _sock) {
    
    int sock, send_len;
    char cmd;
    char msg[MAX_DATA];
    string resp, name;
    
    // * take input from client
    sock = *(int*) _sock;

    // * recv and route input based on header-byte
    /*
        char[] = {1B CMD||NB PAYLOAD||'\0'}
        (*) Expect incoming buffer to be nullterminated
        (*) Expect PAYLOAD does not contain \0
        (*) All outbound resp are nullterminated
    */

    while(recv(sock, msg, MAX_DATA, 0) > 0) {
        cmd = msg[0];
        if (cmd != LIST)
            name = string(msg + 1);

        if (cmd == CREATE) {            // resp={1B STATUS}
            resp = CREATE_resp(name);
            send_len = 2;
        } else if (cmd == DELETE) {     // resp={1B STATUS}
            resp = DELETE_resp(name);
            send_len = 2;
        } else if (cmd == JOIN) {       // resp={1B STATUS||4B PORT||4B N_MEMBERS}
            resp = JOIN_resp(name);
            send_len = 10;
        } else if (cmd == LIST) {       // resp={ROOM1||, ||ROOM2||, ||...}
            resp = LIST_resp();
            send_len = resp.length() + 1;
        } else {
            resp = string((char) FAILURE_INVALID, 1);
        }

        // * send response to client
        if (send(sock, resp.c_str(), send_len, 0) < 0) {
            perror("Failure server resp");
        }
    }

    return 0;
}

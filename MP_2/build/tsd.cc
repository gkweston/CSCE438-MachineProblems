/* ------- server ------- */
#include <ctime>

#include <google/protobuf/timestamp.pb.h>
#include <google/protobuf/duration.pb.h>

#include <fstream>
#include <vector>
#include <iostream>
#include <memory>
#include <string>
#include <stdlib.h>
#include <unistd.h>
#include <google/protobuf/util/time_util.h>
#include <grpc++/grpc++.h>


#include "sns.grpc.pb.h"

// Error codes
// (!) Convert to single char error codes so we don't send extra data on the wire, place in header
#define SUCCESS                     ("SUCCESS")
#define FAILURE_ALREADY_EXISTS      ("FAILURE_ALREADY_EXISTS")
#define FAILURE_NOT_EXISTS          ("FAILURE_NOT_EXISTS")
#define FAILURE_INVALID_USERNAME    ("FAILURE_INVALID_USERNAME")
#define FAILURE_INVALID             ("FAILURE_INVALID")
#define FAILURE_UNKNOWN             ("FAILURE_UNKNOWN")

using google::protobuf::Timestamp;
using google::protobuf::Duration;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerReader;
using grpc::ServerReaderWriter;
using grpc::ServerWriter;
using grpc::Status;
using csce438::Message;
using csce438::Request;
using csce438::Reply;
using csce438::SNSService;
using std::string;
using std::vector;
using std::cout;

/* NOTES
1. for persistant memory coding points, search: (!)data_store
2. Implement EntryTable on Login/Follow/UnFollow
3. covert error msg strings to pseudo-enum #define type
4. If a user's connection is lost we need to evist them from all_users and following_users for each user

*/

// user_table in memory version:
struct UserEntry {
    string username;
    vector<string> following_users;

    // Users start by following themselves
    UserEntry(string n) : username(n) { 
        following_users.push_back(n);
    }
    int is_following(string uname) {
        // Return the idx of the following user or -1 if none
        for (int idx = 0; idx < following_users.size(); idx++) {
            if (following_users[idx] == uname) {
                return idx;
            }
        }
        return -1;
    }
    bool pop_follower(string uname) {
        // Return true if the user was following and was removed
        // or false if the user isn't following
        for (int i = 0; i < following_users.size(); i++) {
            if (following_users[i] == uname) {
                following_users.erase(following_users.begin() + i);
                return true;
            }
        }
        return false;
    }
};

struct EntryTable {
    vector<UserEntry*> table;
    void add_user(string uname) {
        table.push_back(new UserEntry(uname));
    }
    UserEntry* get_user_entry(string uname) {
        for (int i = 0; i < table.size(); i++) {
            if (table[i]->username == uname) {
                return table[i];
            }
        }
        return nullptr;
    }
};

class SNSServiceImpl final : public SNSService::Service {
  
    Status List(ServerContext* context,
                const Request* request,
                Reply* reply) override {
        cout << "S| recvd LIST\n";//(!)

        // Fill the all users protobuf and when we find this user's name
        // we'll save a pointer to their user entry so we can copy
        // following users
        UserEntry* this_user = nullptr;
        for (int i = 0; i < user_entry_table.size(); i++) {
            string uname = user_entry_table[i]->username;
            reply->add_all_users(uname.c_str());
            if (uname == request->username()) {
                this_user = user_entry_table[i];
            }
        }

        // Check if username not found in user_entry_table
        if (!this_user) {
            reply->set_msg(FAILURE_INVALID_USERNAME);
            return Status::OK;
        }

        // Copy following users
        for (int i = 0; i < this_user->following_users.size(); i++) {
            reply->add_following_users(this_user->following_users[i].c_str());
        }
        reply->set_msg(SUCCESS);
        return Status::OK;
    }

    Status Follow(ServerContext* context,
                const Request* request,
                Reply* reply) override {
        /* To test:
            a. If user tries to follow themselves
            b. If user tries to follow a user they already
            c. If user tries to follow a user which doesn't exist(!)
            d. If user tries to follow a user which does exist(!)
        */
        cout << "S| recvd Follow\n";//(!)
        
        /* ------- */
        string uname = request->username();
        string uname_to_follow = request->arguments(0);
        // User is trying to follow themselves, which is done automatically on login
        if (uname_to_follow == uname) {
            reply->set_msg(FAILURE_ALREADY_EXISTS);
            return Status::OK;
        }
        // Check if user is trying to follow one which DNE
        UserEntry* ufollow_entry = get_user_entry(uname_to_follow);
        if (ufollow_entry == nullptr) {
            reply->set_msg(FAILURE_NOT_EXISTS);//(!)
            return Status::OK;
        } 
        // Check if user is trying to follow a user they already follow
        if (ufollow_entry->is_following(uname) == -1) {
            reply->set_msg(FAILURE_ALREADY_EXISTS);
            return Status::OK;
        }

        ufollow_entry->following_users.push_back(uname);
        reply->set_msg(SUCCESS);
        return Status::OK;
    }

    Status UnFollow(ServerContext* context,
                    const Request* request,
                    Reply* reply) override {
        /* To test:
            a. If user tries to unfollow themselves
            b. If user tries to unfollow a user they dont
            c. If user tries to unfollow a user which doesn't exist (!)
            d. If user tries to unfollow a user which does exist(!) table not updated
        */
        cout << "S| recvd UnFollow\n";//(!)
        /* ------- */
        string uname = request->username();
        string unfollow_name = request->arguments(0);

        // Check if user tries to unfollow themselves, which we prevent
        if (uname == unfollow_name) {
            reply->set_msg(FAILURE_INVALID_USERNAME);
            return Status::OK;
        }
        UserEntry* ufollow_entry = get_user_entry(unfollow_name);
        // Check if user tries to follow one which DNE
        if (ufollow_entry == nullptr) {
            reply->set_msg(FAILURE_NOT_EXISTS);
            return Status::OK;
        }

        // Check if user tries to unfollow one which they don't follow
        if (ufollow_entry->pop_follower(uname)) {
            // SUCCESS
            reply->set_msg(SUCCESS);
        } else {
            // Wasn't following
            reply->set_msg(FAILURE_INVALID_USERNAME);
        }
        return Status::OK;
    }
  
    Status Login(ServerContext* context,
                const Request* request,
                Reply* reply) override {

        /* To test
            a. if a user logs in when same username is logged in
        */

        cout << "S| recv Login\n";//(!)
        // * Check if uname already - handle this case... (!)
        string uname = request->username();
        for (int i = 0; i < user_entry_table.size(); i++) {
            if (user_entry_table[i]->username == uname) {
                cout << "ERR: USER ALREADY — handle this case\n";//(!)
                break;
            }
        }

        // * Add to user table and return OK
        UserEntry* uentry = new UserEntry(uname);
        user_entry_table.push_back(uentry);

        string pre("ok, hello ");//(!)
        reply->set_msg(pre + request->username());//(!)
        // reply->set_msg(SUCCESS);
        return Status::OK;
    }

    Status Timeline(ServerContext* context,
                    ServerReaderWriter<Message, Message>* stream) override {
    // ------------------------------------------------------------
    // In this function, you are to write code that handles 
    // receiving a message/post from a user, recording it in a file
    // and then making it available on his/her follower's streams
    // ------------------------------------------------------------
        return Status::OK;
    }
    // vector<string> user_table; //(!) should exist in persistant storage
    vector<UserEntry*> user_entry_table;

    // Returns the user entry for specific username, or null if none found
    UserEntry* get_user_entry(string uname) {
        cout << "Looking for: " << uname << '\n';
        for (int i = 0; i < user_entry_table.size(); i++) {
            cout << "Found: " << user_entry_table[i]->username << '\n';
            if (user_entry_table[i]->username == uname) {
                cout << uname << "==" << user_entry_table[i]->username << '\n';
                return user_entry_table[i];
            }
        }
        cout << uname << " not found\n";
        return nullptr;
    }
};

void RunServer(string port_no) {
    // ------------------------------------------------------------
    // In this function, you are to write code 
    // which would start the server, make it listen on a particular
    // port number.
    // ------------------------------------------------------------

    // (!)data_store
    // * Check if there's a user_data_store file
    // if not, create one
    // if so, read data into user_entry_table vector

    //* Make init service and server builder
    string addr = string("127.0.0.1:") + port_no;
    SNSServiceImpl service;
    ServerBuilder builder;

    /*
    Reflection and stuff? (!)
    */

    //* Listen on given address (insecure) and register service
    builder.AddListeningPort(
        addr,
        grpc::InsecureServerCredentials()
    );
    builder.RegisterService(&service);
    //* Assemble server
    std::unique_ptr<Server> server(builder.BuildAndStart());
    cout << "(!) service listening on " << addr << '\n';
    server->Wait();//(!) never returns
}

int main(int argc, char** argv) {
  
  string port = "3010";
  int opt = 0;
  while ((opt = getopt(argc, argv, "p:")) != -1){
    switch(opt) {
      case 'p':
          port = optarg;
          break;
      default:
	         std::cerr << "Invalid Command Line Argument\n";
    }
  }
  RunServer(port);
  return 0;
}

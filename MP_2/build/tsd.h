#include <vector>
#include <string>
#include <grpc++/grpc++.h>

#include "sns.grpc.pb.h"

// Bad practice, but we only include in tsd.cc so should be fine
using grpc::ServerReaderWriter;
using csce438::Message;

// Error codes - autocomplete helps use make less mistakes :)
#define SUCCESS                     ("SUCCESS")
#define FAILURE_ALREADY_EXISTS      ("FAILURE_ALREADY_EXISTS")
#define FAILURE_NOT_EXISTS          ("FAILURE_NOT_EXISTS")
#define FAILURE_INVALID_USERNAME    ("FAILURE_INVALID_USERNAME")
#define FAILURE_INVALID             ("FAILURE_INVALID")
#define FAILURE_UNKNOWN             ("FAILURE_UNKNOWN")

// Store username->followers in memory which enables us to track the followers for
// a given user and conduct some operations on them
struct User {
    std::string username;
    std::vector<std::string> following_users;
    bool timeline_mode;
    ServerReaderWriter<Message, Message>* stream;

    // Users start by following themselves
    User(std::string n) : username(n) { 
        // following_users.push_back(n);
        following_users = std::vector<std::string>(1, n); //safer
        timeline_mode = false;
        stream = nullptr;
    }
    int is_following(std::string uname) {
        // Return idx of user following,
        //         -1 if none
        for (int idx = 0; idx < following_users.size(); idx++) {
            if (following_users[idx] == uname) {
                return idx;
            }
        }
        return -1;
    }
    bool pop_follower(std::string uname) {
        // Return true: if the user was following and was removed,
        //       false: if the user isn't following
        for (int i = 0; i < following_users.size(); i++) {
            if (following_users[i] == uname) {
                following_users.erase(following_users.begin() + i);
                return true;
            }
        }
        return false;
    }

    void set_stream(ServerReaderWriter<Message, Message>* s) {
        stream = s;
        timeline_mode = true;
    }
};

// struct UserStream {
//     std::string username;
//     grpc::ServerReaderWriter<Message, Message>* stream;
//     UserStream(std::string n, grpc::ServerReaderWriter<Message, Message>* s) {
//         username = n;
//         stream = s;
//     }
// };

// ------- Unimplemented | Untested -------
struct UserTable {
    // We're pointing, because if a user is removed we can no longer
    // refer to their index in users as a pointer
    std::vector<User*> users;

    UserTable() : users(std::vector<User*>()) { }
    ~UserTable() {
        for (int i = 0; i < users.size(); i++) {
            delete users[i];
        }
    }
    User* get_user_entry(std::string uname) { // if pointing, return nullptr
        // Return User* entry in users table
        //        nullptr if not found
        for (int idx = 0; idx < users.size(); idx++) {
            if (users[idx]->username == uname) {
                return users[idx];
            }
        }
        return nullptr;
    }
    void add_user(std::string uname) { users.push_back(new User(uname)); }

    // TODO
    bool read_users(std::string file);
    bool write_users(std::string file);
};
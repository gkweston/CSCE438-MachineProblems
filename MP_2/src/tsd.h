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

    // users which are followers of this user, used for the LIST command
    std::vector<std::string> followers; 

    // users which this user is following, used for message forwarding in TIMELINE
    // which allows us to iterate the users table only once
    std::vector<std::string> following; 

    // For timeline mode
    bool timeline_mode;
    ServerReaderWriter<Message, Message>* stream;

    // Users start by following themselves
    User(std::string n) : username(n) { 
        // followers.push_back(n);
        followers = std::vector<std::string>(1, n); //safer
        following = std::vector<std::string>(1, n);
        timeline_mode = false;
        stream = nullptr;
    }
    int is_follower(std::string uname) {
        // Return idx of user following,
        //         -1 if none
        for (int idx = 0; idx < followers.size(); idx++) {
            if (followers[idx] == uname) {
                return idx;
            }
        }
        return -1;
    }
    bool pop_follower(std::string uname) {
        // Return true: if the user was following and was removed,
        //       false: if the user isn't following
        for (int i = 0; i < followers.size(); i++) {
            if (followers[i] == uname) {
                followers.erase(followers.begin() + i);
                return true;
            }
        }
        return false;
    }
    bool push_following(std::string uname) {
        // check if uname is in following, if not add it
        // Return false: if the user is already in following (didn't add)
        //        true: if use is added
        for (int i = 0; i < following.size(); i++) {
            if (following[i] == uname) {
                false;
            }
        }
        following.push_back(uname);
        return true;
    }
    bool pop_following(std::string uname) {
        // check if uname is in following, if so remove
        // Return false: uname wasn't there
        //        true: if use is added
        for (int i = 0; i < following.size(); i++) {
            if (following[i] == uname) {
                following.erase(following.begin() + i);
                return true;
            }
        }
        return false;
    }
    bool is_following(std::string uname) {
        for (int i = 0; i < following.size(); i++) {
            if (following[i] == uname) {
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

// ------- Unused -------
// struct UserStream {
//     std::string username;
//     grpc::ServerReaderWriter<Message, Message>* stream;
//     UserStream(std::string n, grpc::ServerReaderWriter<Message, Message>* s) {
//         username = n;
//         stream = s;
//     }
// };
// ------- Unimplemented | Untested -------
// struct UserTable {
//     // We're pointing, because if a user is removed we can no longer
//     // refer to their index in users as a pointer
//     std::vector<User*> users;

//     UserTable() : users(std::vector<User*>()) { }
//     ~UserTable() {
//         for (int i = 0; i < users.size(); i++) {
//             delete users[i];
//         }
//     }
//     User* get_user_entry(std::string uname) { // if pointing, return nullptr
//         // Return User* entry in users table
//         //        nullptr if not found
//         for (int idx = 0; idx < users.size(); idx++) {
//             if (users[idx]->username == uname) {
//                 return users[idx];
//             }
//         }
//         return nullptr;
//     }
//     void add_user(std::string uname) { users.push_back(new User(uname)); }

//     // TODO
//     bool read_users(std::string file);
//     bool write_users(std::string file);
// };
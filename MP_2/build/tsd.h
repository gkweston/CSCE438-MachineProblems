#include <vector>
#include <string>
using namespace std;

// Error codes
// (!) Convert to single char error codes so we don't send extra data on the wire, place in header
#define SUCCESS                     ("SUCCESS")
#define FAILURE_ALREADY_EXISTS      ("FAILURE_ALREADY_EXISTS")
#define FAILURE_NOT_EXISTS          ("FAILURE_NOT_EXISTS")
#define FAILURE_INVALID_USERNAME    ("FAILURE_INVALID_USERNAME")
#define FAILURE_INVALID             ("FAILURE_INVALID")
#define FAILURE_UNKNOWN             ("FAILURE_UNKNOWN")

// Store username->followers in memory which enables us to track the followers for
// a given user and conduct some operations on them
struct UserEntry {
    string username;
    vector<string> following_users;

    // Users start by following themselves
    UserEntry(string n) : username(n) { 
        following_users.push_back(n);
    }
    int is_following(string uname) {
        // Return idx of user following,
        //         -1 if none
        for (int idx = 0; idx < following_users.size(); idx++) {
            if (following_users[idx] == uname) {
                return idx;
            }
        }
        return -1;
    }
    bool pop_follower(string uname) {
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
};

struct EntryTable {
    vector<UserEntry*> table;
    void add_user(string uname) {
        table.push_back(new UserEntry(uname));
    }
    UserEntry* get_user_entry(string uname) {
        // Return UserEntry* for given uname,
        //        nullptr if no entry found
        for (int i = 0; i < table.size(); i++) {
            if (table[i]->username == uname) {
                return table[i];
            }
        }
        return nullptr;
    }
};
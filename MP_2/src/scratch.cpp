#include <vector>
#include <string>
#include <fstream>
#include <iostream>
using namespace std;

struct User {
    string username;
    vector<string> followers;
    vector<string> following;
    User(std::string n) : username(n) { 
        // followers.push_back(n);
        followers = std::vector<std::string>(1, n); //safer
        following = std::vector<std::string>(1, n);
    }
    // User() : username("") { }
};

int testof() {
    vector<User*> users;
    User* u1 = new User("u1");
    User* u2 = new User("u2");
    User* u3 = new User("u3");
    
    u1->followers.push_back(u2->username);
    u1->followers.push_back(u3->username);
    u2->followers.push_back(u3->username);
    u2->following.push_back(u1->username);
    u3->following.push_back(u1->username);
    u3->following.push_back(u2->username);
    users.push_back(u1);
    users.push_back(u2);
    users.push_back(u3);
    

    //-------Write
    // Lazymode. Just clear file, write out users vec.
    // Would write to .json, but not sure if grading machine will
    // have the lib
    
    ofstream f;
    f.open("datastore", ofstream::out | ofstream::trunc);
    for (int i = 0; i < users.size(); i++) {
        User* u = users[i];
        // Write name then \n
        f << u->username << '\n';

        // Write all followers delim by ','
        for (int j = 0; j < u->followers.size(); j++) {
            f << u->followers[j] << ',';
        }
        f << '\n';
        
        // Write all following
        for (int j = 0; j < u->following.size(); j++) {
            f << u->following[j] << ',';
        }
        f << '\n';
    }
    
    f.close();
    delete u1;
    delete u2;
    delete u3;
}

void testif() {
    
    vector<User*> users;

    string line;
    ifstream f("datastore");
    int i = 0;
    User* u;
    if (f.is_open()) {
        while (getline(f, line)) {
            // Remove '\n'
            if (!line.empty() && line[line.length()-1] == '\n') {
                line.erase(line.length()-1);
            }
            // Remove trailing ,
            if (!line.empty() && line[line.length()-1] == ',') {
                line.erase(line.length()-1);
            }
            

            if (i % 3 == 0) { //username
                // cout << line << endl;//(!)
                u = new User(line);
                users.push_back(u);
            } else { //followers
                size_t idx = 0;
                string tok;
                while ((idx = line.find(",")) != string::npos) {
                    tok = line.substr(0, idx);
                    if (tok != u->username) {
                        if (i % 3 == 1) {
                            u->followers.push_back(tok);
                        } else {
                            u->following.push_back(tok);
                        }
                    }
                    line.erase(0, idx + 1);
                }
                if (line != u->username) {
                    if (i % 3 == 1) {
                        u->followers.push_back(line);
                    } else {
                        u->following.push_back(line);
                    }
                }

            }
            if (++i == 3) i = 0;
        }
    }
    cout << "---" << endl;
    for (int i = 0; i < users.size(); i++) {
        User* u = users[i];
        cout << users[i]->username << endl;;
        for (int j = 0; j < u->followers.size(); j++) {
            cout << u->followers[j] << ',';
        }
        cout << endl;
        for (int j = 0; j < u->following.size(); j++) {
            cout << u->following[j] << ',';
        }
        cout << endl;
    }

}

int main() {
    // testof();
    testif();
}


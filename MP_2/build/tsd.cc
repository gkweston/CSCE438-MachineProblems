/* ------- server ------- */
#include <ctime>
#include <google/protobuf/timestamp.pb.h>
#include <google/protobuf/duration.pb.h>
#include <google/protobuf/util/time_util.h>
#include <grpc++/grpc++.h>
#include <fstream>
#include <vector>
#include <iostream>
#include <memory>
#include <string>
#include <stdlib.h>
#include <unistd.h>


// #include "sns.grpc.pb.h"
#include "tsd.h"

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
using std::ofstream;
using std::ifstream;
using std::cout;

/* TODO
 * Display last 20 messages!!
 * Data store search: (!)data_store
 * USER_ALREADY case on Login
 * Remove dev prints (!)
 */

class SNSServiceImpl final : public SNSService::Service {
  
    Status List(ServerContext* context, const Request* request, Reply* reply) override {
        // Fill the all_users protobuf, when we find current
        // user's name we save their entry to copy followers
        User* this_user = nullptr;
        for (int i = 0; i < users.size(); i++) {
            string uname = users[i]->username;
            reply->add_all_users(uname.c_str());
            if (uname == request->username()) {
                this_user = users[i];
            }
        }
        // Check if username not found in users
        if (!this_user) {
            reply->set_msg(FAILURE_INVALID_USERNAME);
            return Status::OK;
        }
        // Copy following users
        for (int i = 0; i < this_user->followers.size(); i++) {
            reply->add_following_users(this_user->followers[i].c_str());
        }
        reply->set_msg(SUCCESS);
        return Status::OK;
    }

    Status Follow(ServerContext* context, const Request* request, Reply* reply) override {
        /* To test:
            a. If user tries to follow themselves
            b. If user tries to follow a user they already
            c. If user tries to follow a user which doesn't exist(!)
            d. If user tries to follow a user which does exist(!)
        */
        string uname = request->username();
        string uname_to_follow = request->arguments(0);

        // check if uname_to_follow is in following, if not add it
        get_user_entry(uname)->push_following(uname_to_follow);

        // User is trying to follow themselves, which is done automatically on login
        if (uname_to_follow == uname) {
            reply->set_msg(FAILURE_ALREADY_EXISTS);
            return Status::OK;
        }
        // Check if user is trying to follow one which DNE
        User* ufollow_entry = get_user_entry(uname_to_follow);
        if (ufollow_entry == nullptr) {
            reply->set_msg(FAILURE_NOT_EXISTS);//(!)
            return Status::OK;
        } 
        // Check if user is trying to follow a user they already follow
        if (ufollow_entry->is_follower(uname) == -1) {
            ufollow_entry->followers.push_back(uname);
            reply->set_msg(SUCCESS);
            return Status::OK;    
        }
        reply->set_msg(FAILURE_ALREADY_EXISTS);
        return Status::OK;
    }

    Status UnFollow(ServerContext* context, const Request* request, Reply* reply) override {
        /* To test:
            a. If user tries to unfollow themselves
            b. If user tries to unfollow a user they dont
            c. If user tries to unfollow a user which doesn't exist (!)
            d. If user tries to unfollow a user which does exist(!) table not updated
        */
        string uname = request->username();
        string unfollow_name = request->arguments(0);

        // check if unfollow_name is in following, if so remove it
        get_user_entry(uname)->pop_following(unfollow_name);

        // Check if user tries to unfollow themselves, which we prevent
        if (uname == unfollow_name) {
            reply->set_msg(FAILURE_INVALID_USERNAME);
            return Status::OK;
        }
        User* ufollow_entry = get_user_entry(unfollow_name);
        // Check if user tries to follow one which DNE
        if (ufollow_entry == nullptr) {
            reply->set_msg(FAILURE_NOT_EXISTS);
            return Status::OK;
        }
        // Check if user tries to unfollow one which they don't follow
        if (ufollow_entry->pop_follower(uname)) {
            reply->set_msg(SUCCESS);
        } else {
            reply->set_msg(FAILURE_INVALID_USERNAME);   // Wasn't following
        }
        return Status::OK;
    }
  
    Status Login(ServerContext* context, const Request* request, Reply* reply) override {
        /* To test
            a. if a user logs in when same username is logged in
        */
        // * Check if uname already - handle this case... (!)
        string uname = request->username();
        for (int i = 0; i < users.size(); i++) {
            if (users[i]->username == uname) {
                cout << "ERR: USER ALREADY — handle this case\n";//(!)
                break;
            }
        }
        // * Add to user table and return OK
        User* uentry = new User(uname);
        users.push_back(uentry);
        reply->set_msg(string("oh, hi ") + request->username() + string("!"));//(!)
        return Status::OK;
    }

    Status Timeline(ServerContext* context, ServerReaderWriter<Message, Message>* stream) override {
    /*
        We don't care about cache locallity given that we have to backup
        users and messages to a datastore on (essentially) every message
        so we're using heap memory where it makes coding convenient.
    */
        while (true) {
            // * read message from stream
            Message recv, send;
            while (stream->Read(&recv)) {
               // * take username
               string uname = recv.username();
               string rmsg = recv.msg();
               if (rmsg == "INIT") {
                   // * find username entry in users table
                   mtx.lock();
                   User* user = get_user_entry(uname);
                   if (!user) {
                       cout << "WHOOPS NO USER HERE! " << uname << '\n';// this would be v bad (!)
                   } else {
                        // * save stream in user table, set timeline mode to true
                        user->set_stream(stream);
                   }
                   mtx.unlock();
                   continue; // do not fwd init messages
               }

                cout << ">>>SERVER\n";
                cout << "uname " << uname << "\n";
                cout << "recvd " << rmsg;
                cout << "\n>>>\n";
                // username(!)
                // string msg_to_send = "(" + uname + "): " + rmsg;
                // send.set_msg(msg_to_send);
                // time(!)
                // send.set_username(recv.username());
                // send.set_msg(recv.msg());
                // send.set_allocated_timestamp(recv.timestamp());
                // Timestamp t = recv.timestamp();
                // send.set_allocated_timestamp(&t);
                

                // * for each follower in that user, write a message to their stream
                //   iff the stream is open (the follower is in TIMELINE mode)

                /*
                    We're using User::following vector for message routing as such
                    When user A sends a message
                      iterate through ALL users in user table
                        check if they are following A, if so, forward message

                    NOTE: User::following is different than the User::followers vector
                */
                mtx.lock();
                for (int i = 0; i < users.size(); i++) {
                    // * Check user name for follow
                    User* u = users[i];
                    if (u->is_following(uname)) {
                        if (u->stream) {
                            // u->stream->Write(send);
                            // Forward recv message
                            u->stream->Write(recv);
                        }
                    }
                }
                mtx.unlock();
            }
        }   // * repeat
        cout << ">>>SERVER OUT OF LOOP<<<\n"; // We don't hit this point until client disco
        return Status::OK;
    }

    /* User memory containers and functions */
    vector<User*> users; //(!) should exist in persistant storage
    std::mutex mtx;
    // Returns the user entry for specific username, or null if none found
    User* get_user_entry(string uname) {
        for (int i = 0; i < users.size(); i++) {
            if (users[i]->username == uname) {
                return users[i];
            }
        }
        return nullptr;
    }
    // Would prefer to do IO to .json, but unsure if grading machine
    // will have json.h --- this is a mess... move along! move along!
    bool read_users(string path) {
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
    }
    bool write_users(string path) {
        // Lazymode. Just clear file, write out users vec.
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
    // if so, read data into users vector

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

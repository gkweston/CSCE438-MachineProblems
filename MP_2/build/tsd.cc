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
using std::cout;

/* TODO
 * Message routing with a vector for following and followers
 * Time stamps & message formatting
 * Data store search: (!)data_store
 * USER_ALREADY case on Login
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
        for (int i = 0; i < this_user->following_users.size(); i++) {
            reply->add_following_users(this_user->following_users[i].c_str());
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
        if (ufollow_entry->is_following(uname) == -1) {
            ufollow_entry->following_users.push_back(uname);
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
                string msg_to_send = "(" + uname + "): " + rmsg;
                send.set_msg(msg_to_send);

                // * for each follower in that user, write a message to their stream
                //   iff the stream is open (the follower is in TIMELINE mode)
                mtx.lock();
                for (int i = 0; i < users.size(); i++) {
                    // * Check user name for follow (!)
                    if (!users[i]->stream) {
                        cout << "WHOOPS NO STREAM HERE FOR" << users[i]->username << "...yet?\n";
                    } else {
                        users[i]->stream->Write(send);
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
    bool read_users(string path);//(!)data_store
    bool write_users(string path);//(!)data_store
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

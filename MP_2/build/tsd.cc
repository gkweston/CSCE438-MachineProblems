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

/* NOTES
1. for persistant memory coding points, search: (!)data_store
4. If a user's connection is lost we need to evist them from all_users and following_users for each user
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
    // ------------------------------------------------------------
    // In this function, you are to write code that handles 
    // receiving a message/post from a user, recording it in a file
    // and then making it available on his/her follower's streams
    // ------------------------------------------------------------
        // * read message from stream

        // * take username

        // * find username entry in users table

        // * set that username's TIMELINE mode variable to true (?)

        // * for each follower in that user, write a message to their stream
        //   (?) iff the stream is open (the follower is in TIMELINE mode)

        // * repeat
        // return Status::OK;
        // -------(!)
        // Send some test messages to see if stream works
        Message send1, send2;
        send1.set_username("bokeh");
        send1.set_msg("oh, hi bokeh!");
        send2.set_username("box");
        send2.set_msg("oh, hi box!");

        Message msg_recv;
        while (stream->Read(&msg_recv)) {
            // std::unique_lock<std::mutex> lock(mutx);(!)
            cout << "Hello: " << msg_recv.username() << '\n';
            cout << ">> " << msg_recv.msg() << '\n';
            stream->Write(send1);
            stream->Write(send2);
        }
        return Status::OK;
    }

    /* User memory containers and functions */
    vector<User*> users; //(!) should exist in persistant storage
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

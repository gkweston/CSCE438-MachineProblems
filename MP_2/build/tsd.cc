/* HOST FILE DIFF */
/*REMOTE FILE DIFF*/
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

/*
user_table in memory version:
*/

struct UserEntry {
    string username;
    vector<string> following_users;

    // Users start by following themselves
    UserEntry(string n) : username(n) { 
        following_users.push_back(n);
    }
};

class SNSServiceImpl final : public SNSService::Service {
  
  Status List(ServerContext* context,
              const Request* request,
              Reply* reply) override {
    // ------------------------------------------------------------
    // In this function, you are to write code that handles 
    // LIST request from the user. Ensure that both the fields
    // all_users & following_users are populated
    // ------------------------------------------------------------
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

    // Copy following users
    for (int i = 0; i < this_user->following_users.size(); i++) {
        reply->add_following_users(this_user->following_users[i].c_str());
    }
    
    return Status::OK;
  }

  Status Follow(ServerContext* context,
                const Request* request,
                Reply* reply) override {
    // ------------------------------------------------------------
    // In this function, you are to write code that handles 
    // request from a user to follow one of the existing
    // users
    // ------------------------------------------------------------
    cout << "S| recvd Follow\n";//(!)

    // find the username passed as arg, then add request->username
    // to that persons UserEntry->following_users
    reply->set_msg("FAILURE_NOT_EXISTS");
    string user_to_follow = request->arguments(0);
    for (int i = 0; i < user_entry_table.size(); i++) {
        if (user_entry_table[i]->username == user_to_follow) {
            user_entry_table[i]->following_users.push_back(request->username());
            reply->clear_msg();
            reply->set_msg("SUCCESS");
            break;
        }
    }
    return Status::OK; 
  }

  Status UnFollow(ServerContext* context,
                  const Request* request,
                  Reply* reply) override {
    // ------------------------------------------------------------
    // In this function, you are to write code that handles 
    // request from a user to unfollow one of his/her existing
    // followers
    // ------------------------------------------------------------
    cout << "S| recvd UnFollow\n";//(!)
    return Status::OK;
  }
  
  Status Login(ServerContext* context,
               const Request* request,
               Reply* reply) override {
    // ------------------------------------------------------------
    // In this function, you are to write code that handles 
    // a new user and verify if the username is available
    // or already taken
    // ------------------------------------------------------------
    
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

    string pre("ok, hello ");//(!)
    reply->set_msg(pre + request->username());//(!)
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
};

void RunServer(string port_no) {
  // ------------------------------------------------------------
  // In this function, you are to write code 
  // which would start the server, make it listen on a particular
  // port number.
  // ------------------------------------------------------------
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

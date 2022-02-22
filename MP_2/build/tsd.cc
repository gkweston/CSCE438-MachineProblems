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

/*
user_table in memory version:

struct user_entry {
    string username
    vector following_users
}

vector<user_entry> user_table

*/

class SNSServiceImpl final : public SNSService::Service {
  
  Status List(ServerContext* context,
              const Request* request,
              Reply* reply) override {
    // ------------------------------------------------------------
    // In this function, you are to write code that handles 
    // LIST request from the user. Ensure that both the fields
    // all_users & following_users are populated
    // ------------------------------------------------------------
    std::cout << "S| recvd LIST\n";//(!)

    for (int i = 0; i < user_table.size(); i++) {
        // reply->set_all_users(i, user_table[i].c_str());
        reply->add_all_users(user_table[i].c_str());
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
    std::cout << "S| recvd Follow\n";//(!)
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
    std::cout << "S| recvd UnFollow\n";//(!)
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

    /*
    IF we recv a user that's already in user_table, we simply overwrite
    that user's entry with a fresh one (and/or set the following vec to
    empyt)
    */

    std::cout << "S| recv Login\n";//(!)
    // * Check if uname already - handle this case... (!)
    std::string uname = request->username();
    for (int i = 0; i < user_table.size(); i++) {
        if (user_table[i] == uname) {
            // reply->set_msg(std::string("ALREADY"));
            // return Status::CANCELLED;
            std::cout << "ERR: USER ALREADY — handle this case\n";//(!)
            break;
        }
    }

    // * Add to user table and return OK
    user_table.push_back(uname);
    std::string pre("ok, hello ");
    reply->set_msg(pre + request->username());
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
  std::vector<std::string> user_table; //(!) should exist in persistant storage
};

void RunServer(std::string port_no) {
  // ------------------------------------------------------------
  // In this function, you are to write code 
  // which would start the server, make it listen on a particular
  // port number.
  // ------------------------------------------------------------
  //* Make init service and server builder
  std::string addr = std::string("127.0.0.1:") + port_no;
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
  std::cout << "(!) service listening on " << addr << '\n';
  server->Wait();//(!) never returns
}

int main(int argc, char** argv) {
  
  std::string port = "3010";
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

/*HOST FILE DIFF*/
/*REMOTE FILE DIFF*/
#include <iostream>
#include <string>
#include <unistd.h>
#include <grpc++/grpc++.h>
#include "client.h"
#include "sns.grpc.pb.h"

using csce438::SNSService;
using csce438::Reply;
using csce438::Request;
using csce438::Message;
using grpc::Status;
using grpc::ClientContext;

class Client : public IClient
{
    
    public:
        Client(const std::string& hname,
               const std::string& uname,
               const std::string& p) : 
               hostname(hname), username(uname), port(p),
               stub_(
                    csce438::SNSService::NewStub(
                        grpc::CreateChannel(
                            hostname + ":" + port,
                            grpc::InsecureChannelCredentials()
                        )
                    ) 
                ) { }
               
    protected:
        virtual int connectTo();
        virtual IReply processCommand(std::string& input);
        virtual void processTimeline();
    private:
        std::string hostname;
        std::string username;
        std::string port;
        
        // You can have an instance of the client stub
        // as a member variable.
        // std::unique_ptr<NameOfYourStubClass::Stub> stub_;
        std::unique_ptr<csce438::SNSService::Stub> stub_;
};

/* (!) set fwds and move funcs after main (!) */
/*struct IReply
{
    grpc::Status grpc_status;
    enum IStatus comm_status;
    std::vector<std::string> all_users;
    std::vector<std::string> following_users;
};*/
IReply make_IReply(grpc::Status stat, Reply repl, std::string cmd) {
    
    // * Make IReply and copy grpc::Status
    IReply irepl;
    irepl.grpc_status = stat;

    // * Parse by command, set IStatus and copy relevant data
    if (cmd == "LIST") {

        int n_users = repl.all_users_size();
        if (stat.ok() && n_users > 0)
            irepl.comm_status = SUCCESS;
        else
            irepl.comm_status = FAILURE_UNKNOWN;

        // * Fill all users vector
        for (int i = 0; i < n_users; i++)
            irepl.all_users.push_back(repl.all_users(i));
        // * Fill all following users vector
        for (int i = 0; i < repl.following_users_size(); i++)
            irepl.following_users.push_back(repl.following_users(i));
        
    } else {
        std::cout << "make_IReply not completed\n";//(!)
    }

    return irepl;
}

IReply Client::processCommand(std::string& input)
{
	// ------------------------------------------------------------
	// GUIDE 1:
	// In this function, you are supposed to parse the given input
    // command and create your own message so that you call an 
    // appropriate service method. The input command will be one
    // of the followings:
	//
	// FOLLOW <username>
	// UNFOLLOW <username>
	// LIST
    // TIMELINE
	//
	// ------------------------------------------------------------
    
    // * Inst. request
    Request req;
    req.set_username(username);

    // * Pass context, request, response
    grpc::ClientContext ctx;
    Reply repl;
    Status stat;

    // * Set stub message to the command and issue request to service
	if (input == std::string("FOLLOW")) {
        // req.set_arguments(0, "FOLLOW"); // repeated takes (idx, c_str)
        // std::cout << "Needs TIMELINE mode\n";//(!)
        // stat = stub_->Follow(&ctx, req, &repl);

    } else if (input == std::string("UNFOLLOW")) {
        // std::cout << "Needs TIMELINE mode\n";//(!)
        // stat = stub_->UnFollow(&ctx, req, &repl);

    } else if (input == std::string("LIST")) {
        stat = stub_->List(&ctx, req, &repl);
        

    } else if (input == std::string("TIMELINE")) {
        // std::cout << "Not sending TIMELINE, needs ServerReaderWriter stream\n";//(!)
        // stat = stub_->Timeline(&ctx, req, &repl);
    } else {
        std::cout << "ERR: Unknown command\n";
    }

    
    if (!stat.ok()) {//(!)
        std::cout << "ERR: not OK\n";//(!)
        std::cout << "(!) "<< stat.error_code() << ' ' << stat.error_message() << '\n';//(!)
    } else std::cout << "Status OK\n";//(!)

    // ------------------------------------------------------------
	// GUIDE 2:
	// Then, you should create a variable of IReply structure
	// provided by the client.h and initialize it according to
	// the result. Finally you can finish this function by returning
    // the IReply.
	// ------------------------------------------------------------
    
    /*
    struct IReply {
        grpc::Status grpc_status;
        enum IStatus comm_status;
        std::vector<std::string> all_users;
        std::vector<std::string> following_users;
    };
    */


	// ------------------------------------------------------------
    // HINT: How to set the IReply?
    // Suppose you have "Follow" service method for FOLLOW command,
    // IReply can be set as follow:
    // 
    //     // some codes for creating/initializing parameters for
    //     // service method
    //     IReply ire;
    //     grpc::Status status = stub_->Follow(&context, /* some parameters */);
    //     ire.grpc_status = status;
    //     if (status.ok()) {
    //         ire.comm_status = SUCCESS;
    //     } else {
    //         ire.comm_status = FAILURE_NOT_EXISTS;
    //     }
    //      
    //      return ire;
    // 
    // IMPORTANT: 
    // For the command "LIST", you should set both "all_users" and 
    // "following_users" member variable of IReply.
    // ------------------------------------------------------------
    
    IReply ire;
    return ire;
}

int Client::connectTo() {
    
    // * Issue a Login command to the server to give our username
    // 1 init a request with the given uname
    Request req;
    req.set_username(username);
    
    // 2. pass: Context, request obj, response obj
    grpc::ClientContext ctx;
    Reply repl;
    grpc::Status status = stub_->Login(&ctx, req, &repl);
    
    // * Check the status returned on the RPC
    if (!status.ok()) {
        std::cout << "(!) rpc:Login failed due to:" << '\n';
        std::cout << "(!) "<< status.error_code() << ' ' << status.error_message() << '\n';
        std::cout << "Buffer message: " << repl.msg() << '\n';
        return 0;
    }
    std::cout << "(!) rpc:Login success\n";//(!)
    std:: cout << "(!) reply.msg: " << repl.msg() << '\n';
    return 1;
}

int main(int argc, char** argv) {

    std::string hostname = "localhost";
    std::string username = "default";
    std::string port = "3010";
    int opt = 0;
    while ((opt = getopt(argc, argv, "h:u:p:")) != -1){
        switch(opt) {
            case 'h':
                hostname = optarg; 
                break;
            case 'u':
                username = optarg;
                break;
            case 'p':
                port = optarg;
                break;
            default:
                std::cerr << "Invalid Command Line Argument\n";
        }
    }

    Client myc(hostname, username, port);
    // You MUST invoke "run_client" function to start business logic
    myc.run_client();

    return 0;
}
/*
int Client::connectTo()
{
	// ------------------------------------------------------------
    // In this function, you are supposed to create a stub so that
    // you call service methods in the processCommand/porcessTimeline
    // functions. That is, the stub should be accessible when you want
    // to call any service methods in those functions.
    // I recommend you to have the stub as
    // a member variable in your own Client class.
    // Please refer to gRpc tutorial how to create a stub.
	// ------------------------------------------------------------

    return 1; // return 1 if success, otherwise return -1
}
*/

void Client::processTimeline()
{
	// ------------------------------------------------------------
    // In this function, you are supposed to get into timeline mode.
    // You may need to call a service method to communicate with
    // the server. Use getPostMessage/displayPostMessage functions
    // for both getting and displaying messages in timeline mode.
    // You should use them as you did in hw1.
	// ------------------------------------------------------------

    // ------------------------------------------------------------
    // IMPORTANT NOTICE:
    //
    // Once a user enter to timeline mode , there is no way
    // to command mode. You don't have to worry about this situation,
    // and you can terminate the client program by pressing
    // CTRL-C (SIGINT)
	// ------------------------------------------------------------
}

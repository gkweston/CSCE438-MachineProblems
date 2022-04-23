/*

(!) Replace with MP2 client file

Required:
    Connect to coord with CID
    recv assignment
    connect to master server assigned
*/

#include <iostream>
#include <memory>
#include <thread>
#include <vector>
#include <string>
#include <unistd.h>
#include <grpc++/grpc++.h>
#include "client.h"

#include "sns.grpc.pb.h"
using grpc::Channel;
using grpc::ClientContext;
using grpc::ClientReader;
using grpc::ClientReaderWriter;
using grpc::ClientWriter;
using grpc::Status;
using csce438::Message;
using csce438::ListReply;
using csce438::Request;
using csce438::Reply;
using csce438::SNSService;
using csce438::Assignment;
using csce438::SNSCoordinatorService;

Message MakeMessage(const std::string& username, const std::string& msg) {
    Message m;
    m.set_username(username);
    m.set_msg(msg);
    google::protobuf::Timestamp* timestamp = new google::protobuf::Timestamp();
    timestamp->set_seconds(time(NULL));
    timestamp->set_nanos(0);
    m.set_allocated_timestamp(timestamp);
    return m;
}

class Client : public IClient {
public:
    Client(const std::string& caddr, const std::string& p, const std::string& cid) :
    coord_addr(caddr), port(p), username(cid) {

        active_hostname = "";
        active_port = "";

        // Instantiate coordinator stub here
        coord_stub_ = std::unique_ptr<SNSCoordinatorService::Stub>(
            SNSCoordinatorService::NewStub(
                grpc::CreateChannel(
                    coord_addr, grpc::InsecureChannelCredentials()
                )
            )
        );
    }
        
protected:
    virtual int connectTo();
    virtual IReply processCommand(std::string& input);
    virtual void processTimeline();
private:
    // Coordinator info
    std::string coord_addr;

    // Active server info
    std::string cluster_sid;
    std::string active_hostname;
    std::string active_port;

    std::string username;       // synonym for CID, for now...
    std::string port;
    
    // To be reused when we can
    std::unique_ptr<SNSCoordinatorService::Stub> coord_stub_;
    std::unique_ptr<SNSService::Stub> active_stub_;

    // Coordinator RPCs
    IAssignment FetchAssignment();

    // Helper
    IStatus parse_comm_status(std::string s);

    // Active server RPCs
    IReply Login();
    IReply List();
    IReply Follow(const std::string& username2);
    IReply UnFollow(const std::string& username2);
    void Timeline(const std::string& username);
};

int Client::connectTo()
{

    // Get assigned server from coordinator
    IAssignment iAssigned = FetchAssignment();
    if (!iAssigned.grpc_status.ok()) {
        std::cout << "gRPC FetchAssignment failed.\n";//(!)
        std::cout << iAssigned.grpc_status.error_message() << '\n';//(!)
        std::cout << iAssigned.grpc_status.error_code() << '\n';//(!)
        return -1;
    }

    if (iAssigned.cluster_sid == "404" || iAssigned.hostname == "404" || iAssigned.port == "404") {
        std::cout << "No server found to assign to client\n";//(!)
        return -1;
    }
    
    // Login to assigned server
    IReply ire = Login();
    if(!ire.grpc_status.ok()) {
        std::cout << "Bad login\n";//(!)
        return -1;
    }
    return 1;
}

IAssignment Client::FetchAssignment() {
    Request request;
    request.set_username(username);
    Assignment assigned;
    ClientContext context;

    IAssignment iAssigned;
    iAssigned.grpc_status = coord_stub_->FetchAssignment(&context, request, &assigned);
    iAssigned.cluster_sid = assigned.sid();
    iAssigned.hostname = assigned.hostname();
    iAssigned.port = assigned.port();

    // Set the assigned active server in the class
    cluster_sid = iAssigned.cluster_sid;
    active_hostname = iAssigned.hostname;
    active_port = iAssigned.port;


    return iAssigned;
}

IReply Client::processCommand(std::string& input)
{
    IReply ire;
    std::size_t index = input.find_first_of(" ");
    if (index != std::string::npos) {
        std::string cmd = input.substr(0, index);


        /*
        if (input.length() == index + 1) {
            std::cout << "Invalid Input -- No Arguments Given\n";
        }
        */

        std::string argument = input.substr(index+1, (input.length()-index));

        if (cmd == "FOLLOW") {
            return Follow(argument);
        } else if(cmd == "UNFOLLOW") {
            return UnFollow(argument);
        }
    } else {
        if (input == "LIST") {
            return List();
        } else if (input == "TIMELINE") {
            ire.comm_status = SUCCESS;
            return ire;
        }
    }

    ire.comm_status = FAILURE_INVALID;
    return ire;
}

void Client::processTimeline()
{
    Timeline(username);
}

IReply Client::List() {
    //Data being sent to the server
    Request request;
    request.set_username(username);

    //Container for the data from the server
    ListReply list_reply;

    //Context for the client
    ClientContext context;

    Status status = active_stub_->List(&context, request, &list_reply);
    IReply ire;
    ire.grpc_status = status;
    //Loop through list_reply.all_users and list_reply.following_users
    //Print out the name of each room
    if(status.ok()){
        ire.comm_status = SUCCESS;
        std::string all_users;
        std::string following_users;
        for(std::string s : list_reply.all_users()){
            ire.all_users.push_back(s);
        }
        for(std::string s : list_reply.followers()){
            ire.followers.push_back(s);
        }
    }
    return ire;
}
        
IReply Client::Follow(const std::string& username2) {
    Request request;
    request.set_username(username);
    request.add_arguments(username2);

    Reply reply;
    ClientContext context;

    Status status = active_stub_->Follow(&context, request, &reply);
    IReply ire;
    ire.grpc_status = status;
    ire.comm_status = parse_comm_status(reply.msg());

    return ire;
}

IReply Client::UnFollow(const std::string& username2) {
    Request request;

    request.set_username(username);
    request.add_arguments(username2);

    Reply reply;

    ClientContext context;

    Status status = active_stub_->UnFollow(&context, request, &reply);
    IReply ire;
    ire.grpc_status = status;
    ire.comm_status = parse_comm_status(reply.msg());
    return ire;
}

IStatus Client::parse_comm_status(std::string server_msg) {
    if (server_msg == "SUCCESS") {
        return IStatus::SUCCESS;
    } else if (server_msg == "FAILURE_ALREADY_EXISTS") {
        return IStatus::FAILURE_ALREADY_EXISTS;
    } else if (server_msg == "FAILURE_NOT_EXISTS") {
        return IStatus::FAILURE_NOT_EXISTS;
    } else if (server_msg == "FAILURE_INVALID_USERNAME") {
        return IStatus::FAILURE_INVALID_USERNAME;
    } else if (server_msg == "FAILURE_INVALID") {
        return IStatus::FAILURE_INVALID;
    } else {
        return IStatus::FAILURE_UNKNOWN;
    }
}

IReply Client::Login() {
    Request request;
    request.set_username(username);
    Reply reply;
    ClientContext context;

    // Instantiate active server stub - login info should be set in class already
    active_stub_ = std::unique_ptr<SNSService::Stub>(
        SNSService::NewStub(
            grpc::CreateChannel(
                active_hostname + ":" + active_port, grpc::InsecureChannelCredentials()//(!)simplify to hostname:port
            )
        )
    );

    Status status = active_stub_->Login(&context, request, &reply);

    IReply ire;
    ire.grpc_status = status;
    ire.comm_status = parse_comm_status(reply.msg());
    return ire;
}

void Client::Timeline(const std::string& username) {
    /* (!) TODO (!)
        Add screen refresh, CLEARSCREEN and render all buffered messages such as in last MP
       (!)      (!)
    */
    ClientContext context;

    std::shared_ptr<ClientReaderWriter<Message, Message>> stream(
        active_stub_->Timeline(&context)
    );

    //Thread used to read chat messages and send them to the server
    std::thread writer([username, stream]() {
        std::string input = "Set Stream";
        Message m = MakeMessage(username, input);
        stream->Write(m);
        while (1) {
            input = getPostMessage();
            m = MakeMessage(username, input);
            stream->Write(m);
        }
        stream->WritesDone();
    });

    std::thread reader([username, stream]() {
        Message m;
        while(stream->Read(&m)){
            google::protobuf::Timestamp temptime = m.timestamp();
            std::time_t time = temptime.seconds();
            displayPostMessage(m.username(), m.msg(), time);
        }
    });

    //Wait for the threads to finish
    writer.join();
    reader.join();
}

int main(int argc, char** argv) {

    /*
    Simplified args:
        -c <coordinatorIP>:<coordinatorPort>
        -p <port>
        -i <clientID>
    */
    if (argc == 1) {//(!)
        std::cout << "Calling convention for client:\n\n";
        std::cout << "./tsn_client -c <coordIP>:<coordPort> -p <clientPort> -i <clientID>\n\n";
        return 0;
    }

    std::string coord_host;
    std::string coord_port;
    std::string clientID = "3010";
    int opt = 0;
    while ((opt = getopt(argc, argv, "c:p:i:")) != -1){
        switch(opt) {
            case 'c':
                coord_host = optarg;
                break;
            case 'p':
                coord_port = optarg;
                break;
            case 'i':
                clientID = optarg;
                break;
            default:
                std::cerr << "Invalid Command Line Argument\n";
        }
    }

    Client myc(coord_host, coord_port, clientID);
    myc.run_client();

    return 0;
}
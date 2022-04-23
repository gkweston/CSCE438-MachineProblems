/*(!)
    - Always render 20 messages, even if blank
(!)     */



/* ------- client ------- */
#include <iostream>
#include <string>
#include <unistd.h>
#include <ctime>
#include <vector>
#include <thread>
#include <grpc++/grpc++.h>
#include <google/protobuf/timestamp.pb.h>
#include <google/protobuf/duration.pb.h>
#include <google/protobuf/util/time_util.h>

#include "tsn_client.h"
#include "sns.grpc.pb.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::ClientReader;
using grpc::ClientReaderWriter;
using grpc::ClientWriter;
using grpc::Status;
using csce438::Message;
using csce438::Request;
using csce438::Reply;
using csce438::SNSService;
using csce438::Assignment;
using csce438::SNSCoordinatorService;
using google::protobuf::util::TimeUtil;
using google::protobuf::Timestamp;

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
    std::string coord_addr;
    std::string cluster_sid;

    std::string active_hostname;
    std::string active_port;

    std::string username;
    std::string port;

    // Helpers
    IAssignment FetchAssignment();
    IReply Login();
    IStatus parse_comm_status(std::string s);
    std::vector<std::string> parse_input_str(std::string in, std::string delim=" ");

    // For last 20 messages, this is hacky but whatever
    std::vector<std::string> senderv;
    std::vector<std::string> messagev;
    std::vector<time_t> timev;
    
    std::unique_ptr<SNSCoordinatorService::Stub> coord_stub_;
    std::unique_ptr<SNSService::Stub> active_stub_;
};
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
std::vector<std::string> Client::parse_input_str(std::string in, std::string delim) {
    std::vector<std::string> arg_vec;
    size_t idx = 0;
    std::string tok;

    while ( (idx = in.find(delim) ) != std::string::npos) {
        tok = in.substr(0, idx);
        arg_vec.push_back(tok);
        in.erase(0, idx+delim.length());
    }
    arg_vec.push_back(in);
    return arg_vec;
}
IStatus Client::parse_comm_status(std::string msg) { // one way to do it...
    if (msg == "SUCCESS") 
        return IStatus::SUCCESS;
    else if (msg == "FAILURE_ALREADY_EXISTS")
        return IStatus::FAILURE_ALREADY_EXISTS;
    else if (msg == "FAILURE_NOT_EXISTS")
        return IStatus::FAILURE_NOT_EXISTS;
    else if (msg == "FAILURE_INVALID_USERNAME")
        return IStatus::FAILURE_INVALID_USERNAME;
    else if (msg == "FAILURE_INVALID")
        return IStatus::FAILURE_INVALID;
    else    
        return IStatus::FAILURE_UNKNOWN;
}
IReply Client::processCommand(std::string& input)
{
    // * Inst. request
    Request req;
    req.set_username(username);

    // * Pass context, request, response
    grpc::ClientContext ctx;
    Reply repl;
    Status stat;

    IReply irepl;
    // Recv'd response, but something went wrong, we overwrite this
    // only when we recv a valid resp
    irepl.comm_status = FAILURE_UNKNOWN;

    std::vector<std::string> arg_vec = parse_input_str(input);
    std::string cmd = arg_vec[0];

    // * Set stub message to the command and issue request to service
	if (cmd == "FOLLOW") {
        // Ensure there is no more/less than 1 argument
        if (arg_vec.size() != 2) {
            irepl.comm_status = FAILURE_INVALID; // invalid user input
            return irepl;
        }

        // Set rpc arg as username
        req.add_arguments(arg_vec[1]);
        // Issue RPC and fill out IReply
        stat = active_stub_->Follow(&ctx, req, &repl);
        irepl.grpc_status = stat;
        if (stat.ok()) {
            irepl.comm_status = parse_comm_status(repl.msg());
        } else {
            irepl.comm_status = FAILURE_UNKNOWN; // connection probably terminated on user's end
        }

    }
    else if (cmd == "UNFOLLOW") {
        // Parse input and ensure FOLLOW arg
        if (arg_vec.size() != 2) {
            irepl.comm_status = FAILURE_INVALID;
            return irepl;
        }

        // Set rpc arg as username
        req.add_arguments(arg_vec[1]);
        // Issue RPC and fill IReply
        stat = active_stub_->UnFollow(&ctx, req, &repl);
        irepl.grpc_status = stat;
        if (stat.ok()) {
            irepl.comm_status = parse_comm_status(repl.msg());
        } else {
            irepl.comm_status = FAILURE_UNKNOWN;
        }

    }
    else if (cmd == "LIST") {
        // * Dispatch LIST req
        stat = active_stub_->List(&ctx, req, &repl);
        irepl.grpc_status = stat;

        // * Parse by command, set IStatus and copy relevant data
        if (stat.ok()) {
            irepl.comm_status = parse_comm_status(repl.msg());
        } else {
            irepl.comm_status = FAILURE_UNKNOWN;
        }
            

        if (irepl.comm_status == SUCCESS) {
            // * Fill all users std::vector
            for (int i = 0; i < repl.all_users_size(); i++)
                irepl.all_users.push_back(repl.all_users(i));
            // * Fill all following users std::vector
            for (int i = 0; i < repl.following_users_size(); i++)
                irepl.followers.push_back(repl.following_users(i));
        }

    }
    else if (cmd == "TIMELINE") {
        // Faking a good TIMELINE receipt to test processTimeline
        irepl.grpc_status = grpc::Status::OK;
        irepl.comm_status = SUCCESS;
    }
    else {
        irepl.comm_status = FAILURE_INVALID; // invalid user input
    }
    return irepl;
}
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
void Client::processTimeline() {
    // Send msg, get resp, repeat
    std::string uname = username;

    ClientContext ctx;
    std::shared_ptr<grpc::ClientReaderWriter<Message, Message>> stream (
        active_stub_->Timeline(&ctx)
    );

    while (true) {
        std::thread writer([&]() {
            // * Init connection, don't bother timestamping inits
            Message client_msg;
            client_msg.set_username(uname);
            client_msg.set_msg("INIT");

            // All remaining messages are taken from stdin
            while (stream->Write(client_msg)) {
                
                
                // client_msg.set_msg(getPostMessage());
                
                std::string outbound_msg = getPostMessage();
                outbound_msg.erase(std::remove(outbound_msg.begin(), outbound_msg.end(), '\n'), outbound_msg.end());
                client_msg.set_msg(outbound_msg);

                Timestamp t = Timestamp();
                client_msg.release_timestamp();
                client_msg.set_allocated_timestamp(&t);
            }
            stream->WritesDone();
        });
        std::thread reader([&]() {
            Message serv_msg;
            while (stream->Read(&serv_msg)) {
                // * Extract sender, msg, time
                std::string post_user = serv_msg.username();
                std::string post_msg = serv_msg.msg();
                time_t post_time = TimeUtil::TimestampToTimeT(serv_msg.timestamp());

                // * Check if we're above 20 messages
                if (senderv.size() > 19) {
                    senderv.erase(senderv.begin());
                    messagev.erase(messagev.begin());
                    timev.erase(timev.begin());
                }
                // * Add to our buffer
                senderv.push_back(post_user);
                messagev.push_back(post_msg);
                timev.push_back(post_time);

                // * Clear screen
                std::system("clear");

                // * Render messages
                std::cout << "+-------\n";
                for (int i = 0; i < senderv.size(); ++i) {
                    std::cout << "| ";
                    displayPostMessage(senderv[i], messagev[i], timev[i]);
                }
                int n_clear_space = 20 - senderv.size();
                for (int i = 0; i < 20 - senderv.size(); ++i) {
                    std::cout << "|\n";
                }
                std::cout << "+-------\n";
            }
        });
        reader.join();
        writer.join();    
    }
    

    Status stat = stream->Finish();
    if (!stat.ok()) {
        std::cout << "Stream failed...\n";
    }

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
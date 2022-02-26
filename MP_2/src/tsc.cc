/* ------- client ------- */
#include <iostream>
#include <string>
#include <unistd.h>
#include <ctime>
#include <google/protobuf/timestamp.pb.h>
#include <google/protobuf/duration.pb.h>
#include <google/protobuf/util/time_util.h>
#include <grpc++/grpc++.h>
#include <vector>
#include "client.h"
#include "sns.grpc.pb.h"
#include <thread>

using google::protobuf::util::TimeUtil;
using google::protobuf::Timestamp;
using csce438::SNSService;
using csce438::Reply;
using csce438::Request;
using csce438::Message;
using grpc::Status;
using grpc::ClientContext;
using std::vector;
using std::string;
using std::cout;

class Client : public IClient {
    public:
    Client(const string& hname, const string& uname, const string& p) {
        hostname = hname;
        username = uname;
        port = p;
        stub_ = csce438::SNSService::NewStub(
            grpc::CreateChannel(
                hostname + ":" + port,
                grpc::InsecureChannelCredentials()
            )
        );
    }
    protected:
    virtual int connectTo();
    virtual IReply processCommand(string& input);
    virtual void processTimeline();

    private:
    string hostname;
    string username;
    string port;

    // For last 20 messages, this is hacky but whatever
    std::vector<string> senderv;
    std::vector<string> messagev;
    std::vector<time_t> timev;
    
    std::unique_ptr<csce438::SNSService::Stub> stub_;
};

vector<string> parse_input_string(string in, string delim=" ") {
    vector<string> arg_vec;
    size_t idx = 0;
    string tok;

    while ( (idx = in.find(delim) ) != string::npos) {
        tok = in.substr(0, idx);
        arg_vec.push_back(tok);
        in.erase(0, idx+delim.length());
    }
    arg_vec.push_back(in);
    return arg_vec;
}

IStatus get_comm_stat(string msg) { // one way to do it...
    if (msg == "SUCCESS") return IStatus::SUCCESS;
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

IReply Client::processCommand(string& input)
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

    vector<string> arg_vec = parse_input_string(input);
    string cmd = arg_vec[0];

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
        stat = stub_->Follow(&ctx, req, &repl);
        irepl.grpc_status = stat;
        if (stat.ok()) {
            irepl.comm_status = get_comm_stat(repl.msg());
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
        stat = stub_->UnFollow(&ctx, req, &repl);
        irepl.grpc_status = stat;
        if (stat.ok()) {
            irepl.comm_status = get_comm_stat(repl.msg());
        } else {
            irepl.comm_status = FAILURE_UNKNOWN;
        }

    }
    else if (cmd == "LIST") {
        // * Dispatch LIST req
        stat = stub_->List(&ctx, req, &repl);
        irepl.grpc_status = stat;

        // * Parse by command, set IStatus and copy relevant data
        if (stat.ok()) {
            irepl.comm_status = get_comm_stat(repl.msg());
        } else {
            irepl.comm_status = FAILURE_UNKNOWN;
        }
            

        if (irepl.comm_status == SUCCESS) {
            // * Fill all users vector
            for (int i = 0; i < repl.all_users_size(); i++)
                irepl.all_users.push_back(repl.all_users(i));
            // * Fill all following users vector
            for (int i = 0; i < repl.following_users_size(); i++)
                irepl.following_users.push_back(repl.following_users(i));
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
        return 0;
    }
    return 1;
}

int main(int argc, char** argv) {

    string hostname = "localhost";
    string username = "default";
    string port = "3010";
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

void Client::processTimeline() {
    // Send msg, get resp, repeat
    string uname = username;

    ClientContext ctx;
    std::shared_ptr<grpc::ClientReaderWriter<Message, Message>> stream (
        stub_->Timeline(&ctx)
    );

    while (true) {
        std::thread writer([&]() {
            // * Init connection, don't bother timestamping inits
            Message client_msg;
            client_msg.set_username(uname);
            client_msg.set_msg("INIT");

            // All remaining messages are taken from stdin
            while (stream->Write(client_msg)) {
                client_msg.set_msg(getPostMessage());
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
                string post_user = serv_msg.username();
                string post_msg = serv_msg.msg();
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

                // * Print in backwards order
                for (int i = senderv.size() - 1; i >= 0; i--) {
                    displayPostMessage(senderv[i], messagev[i], timev[i]);
                }
                // displayPostMessage(post_user, post_msg, post_time);
            }
        });
        reader.join();
        writer.join();    
    }
    

    Status stat = stream->Finish();
    if (!stat.ok()) {
        cout << "Stream failed...\n";
    }

}
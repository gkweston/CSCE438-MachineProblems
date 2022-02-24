/* ------- client ------- */
#include <iostream>
#include <string>
#include <unistd.h>
#include <grpc++/grpc++.h>
#include <vector>
#include "client.h"
#include "sns.grpc.pb.h"

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
    
    std::unique_ptr<csce438::SNSService::Stub> stub_;
};

/* (!) set fwds and move funcs after main (!) */
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
	// ------------------------------------------------------------
	// FOLLOW <username>
	// UNFOLLOW <username>
	// LIST
    // TIMELINE
	// ------------------------------------------------------------
    
    // * Inst. request
    Request req;
    req.set_username(username);

    // * Pass context, request, response
    grpc::ClientContext ctx;
    Reply repl;
    Status stat;

    IReply irepl;
    // This means an invalid response (but we dispatched RPC) so we
    // only overwrite this when we have a Status::OK or the user didn't
    // input a valid cmd, never dispatched RPC
    irepl.comm_status = FAILURE_UNKNOWN;

    vector<string> arg_vec = parse_input_string(input);
    string cmd = arg_vec[0];

    // * Set stub message to the command and issue request to service
	if (cmd == "FOLLOW") {
        // Ensure there is no more/less than 1 argument
        if (arg_vec.size() != 2) {
            cout << "Takes FOLLOW <username>\n";
            cout << "ERR: cover this case";
            return irepl;//(!)
        }

        // Set rpc arg as username
        req.add_arguments(arg_vec[1]);
        // Issue RPC and fill out IReply
        stat = stub_->Follow(&ctx, req, &repl);
        irepl.grpc_status = stat;
        if (stat.ok()) {
            irepl.comm_status = get_comm_stat(repl.msg());
        } else {//(!)
            irepl.comm_status = FAILURE_UNKNOWN;
            cout << "failed with error message:\n" << stat.error_message() << "\n and code: " << stat.error_code() << '\n';//(!)
        }

    }
    else if (cmd == "UNFOLLOW") {
        // Parse input and ensure FOLLOW arg
        if (arg_vec.size() != 2) {
            cout << "Takes FOLLOW <username\n";
            cout << "ERR: cover this case";
            return irepl;//(!)
        }

        // Set rpc arg as username
        req.add_arguments(arg_vec[1]);
        // Issue RPC and fill IReply
        stat = stub_->UnFollow(&ctx, req, &repl);
        irepl.grpc_status = stat;
        if (stat.ok()) {
            irepl.comm_status = get_comm_stat(repl.msg());
        } else {//(!)
            irepl.comm_status = FAILURE_UNKNOWN;
            cout << "failed with error message:\n" << stat.error_message() << "\n and code: " << stat.error_code() << '\n';//(!)
        }

    }
    else if (cmd == "LIST") {
        
        stat = stub_->List(&ctx, req, &repl);
        irepl.grpc_status = stat;

        // * Parse by command, set IStatus and copy relevant data
        
        if (stat.ok()) {
            irepl.comm_status = get_comm_stat(repl.msg());
        } else {//(!)
            irepl.comm_status = FAILURE_UNKNOWN;
            cout << "failed with error message:\n" << stat.error_message() << "\n and code: " << stat.error_code() << '\n';//(!)
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
        // cout << "Not sending TIMELINE, needs ServerReaderWriter stream\n";//(!)
        // stat = stub_->Timeline(&ctx, req, &repl);
        cout << "TIMELINE UNDONE\n";
    }
    else {
        // These means the user input is invalid
        irepl.comm_status = FAILURE_INVALID;
    }

    cout << "stub->msg: <" << repl.msg() << ">\n";//(!)
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
    std:: cout << "logged in: " << repl.msg() << '\n';//(!)
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

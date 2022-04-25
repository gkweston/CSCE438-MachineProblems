/* ------- client ------- */
#include <iostream>
#include <string>
#include <ctime>
#include <vector>
#include <thread>
#include <chrono>
#include <sys/ioctl.h>
#include <unistd.h>

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

#define DEADLINE_MS     (1000)
#define DEBUG           (0)

// Forward
void term_width_break();

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
    IReply Login(bool isFirst);
    IStatus parse_comm_status(std::string s);
    std::vector<std::string> parse_input_str(std::string in, std::string delim=" ");
    void SingleMsgTimelineStream(const std::string& user_in);
    void pretty_print_messages();

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
IReply Client::Login(bool isFirst=false) {

    // * If this is the user's first login, the server will send the last 20 
    //   messages of their timeline (reconnection case)
    Request request;
    request.set_username(username);

    // * Set first arg iff this is our first time connecting to this server, this will
    //   set our timeline as unread so we are served our previous messages
    if (isFirst) {
        request.add_arguments("first");
    }

    Reply reply;
    ClientContext context;

    // Instantiate active server stub - login info should be set in class already
    active_stub_ = std::unique_ptr<SNSService::Stub>(
        SNSService::NewStub(
            grpc::CreateChannel(
                active_hostname + ":" + active_port, grpc::InsecureChannelCredentials()
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
    else {
        if(DEBUG) std::cout << "parse_comm_failure msg=" << msg << "\n";
        return IStatus::FAILURE_UNKNOWN;
    }

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
        std::cerr << "gRPC FetchAssignment failed.\n";
        return -1;
    }

    if (iAssigned.cluster_sid == "404" || iAssigned.hostname == "404" || iAssigned.port == "404") {
        std::cerr << "Server 404 Not Found\n";
        return -1;
    }
    
    // Login to assigned server
    IReply ire = Login(true);
    if(!ire.grpc_status.ok()) {
        std::cerr << "Login Failed\n";
        return -1;
    }
    return 1;
}
void Client::SingleMsgTimelineStream(const std::string& user_in) {
    ClientContext ctx;
    std::shared_ptr<grpc::ClientReaderWriter<Message, Message>> stream {
        active_stub_->Timeline(&ctx)
    };

    // * Send init message, always first
    Message init_msg;
    init_msg.set_username(username);
    init_msg.set_msg("INIT");
    stream->Write(init_msg);

    // * Send the outbound message we just got from user
    Message outbound_msg;
    outbound_msg.set_username(username);
    // outbound_msg.set_msg(getPostMessage());
    outbound_msg.set_msg(user_in);
    stream->Write(outbound_msg);
    stream->WritesDone();

    // * Read any inbound messages, do not render yet
    Message inbound_msg;
    while(stream->Read(&inbound_msg)) {
        std::string post_user = inbound_msg.username();
        std::string post_msg = inbound_msg.msg();
        time_t post_time = TimeUtil::TimestampToTimeT(inbound_msg.timestamp());

        // * Check if we're above 20 messages
        if (senderv.size() > 19) {
            senderv.erase(senderv.begin());
            messagev.erase(messagev.begin());
            timev.erase(timev.begin());
        }
        // * Add to our buffers
        senderv.push_back(post_user);
        messagev.push_back(post_msg);
        timev.push_back(post_time);
    }
    stream->Finish();
}
void Client::pretty_print_messages() {

    // * Clear screen
    std::system("clear");

    // * Print break
    term_width_break();

    for (int i = 0; i < senderv.size(); ++i) {
        displayPostMessage(senderv[i], messagev[i], timev[i]);
    }
    int n_clear_space = 20 - senderv.size();
    for (int i = 0; i < 20 - senderv.size(); ++i) {
        std::cout << "\n";
    }

    term_width_break();
    std::cout << ">>> Press [Enter] to send & fetch messages >>>\n";
}
void Client::processTimeline() {
    std::cout << ">>> Press [Enter] to send & fetch messages >>>\n";
    while(true) {
        // SingleMsgTimelineStream();
        // * Collect input from user, this could scale to multiple messages -- block
        std::string user_in = getPostMessage();
        user_in.erase(std::remove(user_in.begin(), user_in.end(), '\n'), user_in.end());

        // * Checkin with the coordinator for active server
        ClientContext ctx;
        Request req;
        req.set_username(username);
        Assignment assigned;
        Status stat = coord_stub_->FetchAssignment(&ctx, req, &assigned);

        // * If active port has changed, make a new active stub
        if (assigned.port() != active_port) {
            active_port = assigned.port();
            IReply irepl = Login();
            if (!irepl.grpc_status.ok()) {
                if(DEBUG) std::cout << "something bad happend on secondary server login...\n";
            }
        }

        // * Open a timeline stream with active server which sends this message and fetches new ones
        SingleMsgTimelineStream(user_in);

        // * Render these messages to the user
        pretty_print_messages();
    }
}

void term_width_break() {
    // * Get window size and render messages
    winsize wsize;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &wsize);
    int width = wsize.ws_col;
    
    // Print by terminal width
    std::cout << "+";
    for (int i = 0; i < width-2; ++i) {
        std::cout << "-";
    }
    std::cout << "+\n";
}

bool is_numeric(const std::string& s) {
    return !s.empty() &&
        std::find_if(   s.begin(),
                        s.end(),
                        [](unsigned char c) { return !std::isdigit(c); }) == s.end();
}

int main(int argc, char** argv) {

    std::string helper = 
        "Calling convention for client:\n\n"
        "./tsn_client -c <coordIP>:<coordPort> -p <clientPort> -i <clientID>\n\n";

    if (argc == 1) {
        std::cout << helper;        
        return 0;
    }

    std::string coord_host;
    std::string coord_port;
    std::string clientID = "777";
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
                std::cerr << helper;
        }
    }

    if (!is_numeric(clientID)) {
        std::cout << "CID must be a numeric value, exiting...\n";
        return 0;
    }

    Client myc(coord_host, coord_port, clientID);
    myc.run_client();

    return 0;
}
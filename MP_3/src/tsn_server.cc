/* ------- server ------- */

/*
    Add client stub
    Add RegisterServer on init
    Check RegisterServer works

    When client connects, if timeline is there, serve 20 to them, else create timeline
    Write all outbound messages to timeline (user follows self)
    Write all outbound messages to sent_messages.tmp
    
    FOLLOW
        add to following
        if local user
            write to .../$CID/following.data
        if global user
            issue RPC::Coordinator::FollowUpdate
            write to .../$CID/following.data

    LIST

    UNFOLLOW

    TIMELINE

*/

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

#include "tsn_server.h"
#include "tsn_database.h"
#include "tsn_coordinator.h"
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
using grpc::ClientContext;

using csce438::Message;
using csce438::Request;
using csce438::Reply;
using csce438::SNSService;
using csce438::Registration;
using csce438::SNSCoordinatorService;

#define DEFAULT_HOST 				(std::string("0.0.0.0"))
#define FILE_DELIM   				(std::string("|:|"))

class SNSServiceImpl final : public SNSService::Service {
    Status List(ServerContext* context, const Request* request, Reply* reply) override {
        // Fill the all_users protobuf, when we find current
        // user's name we save their entry to copy followers
        User* this_user = nullptr;
        for (int i = 0; i < users.size(); i++) {
            std::string uname = users[i].username;
            reply->add_all_users(uname.c_str());
            if (uname == request->username()) {
                this_user = &users[i];
            }
        }
        // Check if username not found in users
        if (this_user == nullptr) {
            reply->set_msg(FAILURE_INVALID_USERNAME);
            return Status::OK;
        }
        // Copy following users
        for (int i = 0; i < this_user->followers.size(); i++) {
            reply->add_following_users(this_user->followers[i].c_str());
        }
        reply->set_msg(SUCCESS);
        return Status::OK;
    }

    Status Follow(ServerContext* context, const Request* request, Reply* reply) override {
        /* To test:
            a. If user tries to follow themselves
            b. If user tries to follow a user they already
            c. If user tries to follow a user which doesn't exist
            d. If user tries to follow a user which does exist
        */
        std::string uname = request->username();
        std::string uname_to_follow = request->arguments(0);

        // check if uname_to_follow is in following, if not add it
        get_user_entry(uname)->push_following(uname_to_follow);

        // User is trying to follow themselves, which is done automatically on login
        if (uname_to_follow == uname) {
            reply->set_msg(FAILURE_ALREADY_EXISTS);
            return Status::OK;
        }
        // Check if user is trying to follow one which DNE
        User* ufollow_entry = get_user_entry(uname_to_follow);
        if (ufollow_entry == nullptr) {
            reply->set_msg(FAILURE_NOT_EXISTS);
            return Status::OK;
        } 
        // Check if user is trying to follow a user they already follow
        if (ufollow_entry->is_follower(uname) == -1) {
            ufollow_entry->followers.push_back(uname);
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
            c. If user tries to unfollow a user which doesn't exist
            d. If user tries to unfollow a user which does exist
        */
        std::string uname = request->username();
        std::string unfollow_name = request->arguments(0);

        // check if unfollow_name is in following, if so remove it
        get_user_entry(uname)->pop_following(unfollow_name);

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
        // * Check if uname already, use the previous entry in memory
        std::string uname = request->username();
        for (int i = 0; i < users.size(); i++) {
            if (users[i].username == uname) {
                return Status::OK;
            }
        }
        // * Add to user table and return OK
        User uentry(uname);
        users.push_back(uentry);
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
               std::string uname = recv.username();
               std::string rmsg = recv.msg();
               if (rmsg == "INIT") {
                   // * find username entry in users table
                   User* user = get_user_entry(uname);
                   if (user != nullptr) {
                        // * save stream in user table, set timeline mode to true
                        user->set_stream(stream);
                   }
                   continue; // do not fwd init messages
               }
                // * for each follower in that user, write a message to their stream
                //   iff the stream is open (the follower is in TIMELINE mode)
                /*
                    We're using User::following vector for message routing as such
                    When user A sends a message
                      iterate through ALL users in user table
                        check if they are following A, if so, forward message

                    NOTE: User::following is different than the User::followers vector
                */
                for (int i = 0; i < users.size(); i++) {
                    // * Check user name for follow
                    User* u = &users[i];
                    if (u->is_following(uname)) {
                        if (u->stream) {
                            // u->stream->Write(send);
                            // Forward recv message
                            u->stream->Write(recv);
                        }
                    }
                }
            }
        }   // * repeat
        return Status::OK;
    }

    std::string coordinator_addr;
    std::string cluster_sid;
    std::string port;
    ServerType type_at_init;
    std::vector<User> users;  //(!) maybe just make this std::vector<User> and take by & where needed (!)

    std::unique_ptr<SNSCoordinatorService::Stub> coord_stub_;
	void RegisterWithCoordinator();
    User* get_user_entry(const std::string& uname);
public:
    SNSServiceImpl(std::string coord_addr, std::string p, std::string sid, ServerType t);
};
SNSServiceImpl::SNSServiceImpl(std::string coord_addr, std::string p, std::string sid, ServerType t) {
	// Server descriptors
	coordinator_addr = coord_addr;
	port = p;
	cluster_sid = sid;

	// This may change as server(s) fault, but we'll track what this server was
	// spun up as [PRIMARY|SECONDARY]
	type_at_init = t;
	
	// Generate coordinator stub here so we can reuse
	coord_stub_ = std::unique_ptr<SNSCoordinatorService::Stub>(
		SNSCoordinatorService::NewStub(
			grpc::CreateChannel(
				coordinator_addr, grpc::InsecureChannelCredentials()
			)
		)
	);

	// Send registration message
	RegisterWithCoordinator();
}
void SNSServiceImpl::RegisterWithCoordinator() {
	/* Register server w/ coordinator, add me to the routing table(s)! */

	// * Fill RPC metadata and reg msg
	Registration reg;
	reg.set_sid(cluster_sid);
	reg.set_hostname(DEFAULT_HOST);
	reg.set_port(port);
	
	std::string server_type_str = type_to_string(type_at_init);
	std::cout << "Registering as type=" << server_type_str << '\n';//(!)
	reg.set_type(server_type_str);

	Reply repl;
	ClientContext ctx;

	// * Dispatch registration RPC
	Status stat = coord_stub_->RegisterServer(&ctx, reg, &repl);
	if (!stat.ok()) {
		std::cerr << "Server registration error for:\nsid=" << cluster_sid << "\ntype=" << type_at_init << "\n";//(!)
	}

	// * Init SchmokieFS file
	SchmokieFS::PrimaryServer::init_server_fs(cluster_sid);
}
User* SNSServiceImpl::get_user_entry(const std::string& uname) {
    for (int i = 0; i < users.size(); i++) {
        if (users[i].username == uname) {
            return &users[i];
        }
    }
    return nullptr;
}

void RunServer(std::string coord_addr, std::string sid, std::string port_no, ServerType type) {
	// Spin up server instance
	std::string server_address = DEFAULT_HOST + ":" + port_no;  //(!) take as arg
	// ------->>>(!)
	// SNSServiceImpl service;
	// OR
	SNSServiceImpl service(coord_addr, port_no, sid, type);
	// <<<-------(!)

	ServerBuilder builder;
	builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
	builder.RegisterService(&service);
	std::unique_ptr<Server> server(builder.BuildAndStart());
	std::cout << "Server listening on " << server_address << std::endl;

	server->Wait();
}

int main(int argc, char** argv) {

	/*
	Simplified args:
		-c <coordinatorIP>:<coordinatorPort>
		-p <port>
		-i <serverID>		should be a std::string of integers
		-t <type>			[master|primary] or anything else to assign as secondary
	*/

	if (argc == 1) { //(!)
		std::cout << "Calling convention for server:\n\n";
		std::cout << "./tsn_server -c <coordIP>:<coordPort> -p <serverPort> -i <serverID> -t <serverType>\n\n";
		return 0;
	}
  
	std::string port = "3010";
	std::string coord;
	std::string serverID;
	ServerType type;

	int opt = 0;
	while ((opt = getopt(argc, argv, "c:p:i:t:")) != -1){
		switch(opt) {
			case 'c':
				coord = optarg;
				break;
			case 'p':
				port = optarg;
				break;
			case 'i':
				serverID = optarg;
				break;
			case 't':
				type = parse_type(std::string(optarg));
				break;
			default:
				std::cerr << "Invalid Command Line Argument\n";
		}
	}

	if (type == ServerType::NONE) {
		std::cout << "Server type error\n";//(!)
		return 1;
	}
	
	RunServer(coord, serverID, port, type);
	return 0;
}

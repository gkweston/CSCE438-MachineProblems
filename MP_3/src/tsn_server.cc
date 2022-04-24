/* ------- server ------- */

/*
    FOLLOW - Only track followers.data, we don't care about following.data

        Send FollowUpdate(user, user_to_follow) to Coordinator
        Coordinator does ClientEntry[user_to_follow].followers += user
        When SyncService checks in the next time it
            UpdatesAllFollowers in memory
            Writes these to all .../$CID/followers.data


    LIST

        Read from .../$SID/primary/global_clients.data and fill reply
        Read from .../$CID/followers.data and fill reply

    UNFOLLOW

        Unimplemented at this stage. Might be as simple as RPC::FollowUpdate(u1, u2, unfollow),
        but we would want to check that this doesn't mess up any of the *Entry table vectors
        

    TIMELINE
        if a user connects that already has timeline file, send latest 20 msgs
        else, make a new stream

        when a user sends, write to sent_messages.data (local or global)

        periodically check the .../$CID/timeline.data for new messages and send 
        along stream
*/

// (!) change all username to cid
// (!) implement only numeric username/cid
// (!) safelock class that wraps the SchmokieFS IO functions
//     and issues lock/lease RPCs
// (!) or implement a safelock mechanism in SchmokieFS

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
using csce438::GlobalUsers;
using csce438::SNSCoordinatorService;

#define DEFAULT_HOST 				(std::string("0.0.0.0"))
#define FILE_DELIM   				(std::string("|:|"))

class SNSServiceImpl final : public SNSService::Service {

    // RE(!) Huge non datastore shortcut to debug other componenets
    Status List(ServerContext* context, const Request* request, Reply* reply) override {
        // Instead of issuing, try reading from mem like we're aiming to do...

        // * Read all from .../$SID/primary/global_clients.data
        std::vector<std::string> glob_clients = SchmokieFS::PrimaryServer::read_global_clients(cluster_sid);
        for (const std::string& cid_: glob_clients) {
            reply->add_all_users(cid_);
        }

        // * Read all from .../$CID/followers.data
        // (!)(!) Calls a SyncService method here. Not allowed!
        std::vector<std::string> followers = SchmokieFS::SyncService::read_followers_by_cid(cluster_sid, request->username(), "primary"); 
        for (const std::string& cid_: followers) {
            reply->add_following_users(cid_);
        }

        reply->set_msg("SUCCESS");

        return Status::OK;

        //(!)-------------------------------------------------(!)
        //(!)-------------------------------------------------(!)
        //(!)-------------------------------------------------(!)
        // // (!)(!)(!) Refactor to use datastore, try different SyncService refresh frequencies

        // // * Get global users from coordinator
        // Request req;
        // // req.set_username("SYNC");//(!)
        // req.set_username(request->username());
        // req.add_arguments("SERVER");
        // GlobalUsers glob;
        // ClientContext ctx;
        // Status stat = coord_stub_->FetchGlobalClients(&ctx, req, &glob);
        // if (!stat.ok()) {
        //     std::cout << "ERR on FetchGlobalClients for server\n";//(!)
        // }

        // // * Copy to response
        // for (int i = 0; i < glob.cid_size(); ++i) {
        //     reply->add_all_users(glob.cid(i));
        // }
        

        // // * Get followers from coord
        // Request req2;
        // req2.set_username(request->username());
        // req2.add_arguments("SERVER");
        // Reply repl2;
        // ClientContext ctx2;
        // Status stat2 = coord_stub_->FetchFollowers(&ctx2, req2, &repl2);
        // if (!stat2.ok()) {
        //     std::cout << "ERR on FetchFollowers for server\n";//(!)
        // }

        // // * Copy to response
        // for (int i = 0; i < repl2.following_users_size(); ++i) {
        //     reply->add_following_users(repl2.following_users(i));
        // }

        // return Status::OK;
        //(!)-------------------------------------------------(!)
        //(!)-------------------------------------------------(!)
        //(!)-------------------------------------------------(!)

        // // Fill the all_users protobuf, when we find current
        // // user's name we save their entry to copy followers
        // User* this_user = nullptr;
        // for (int i = 0; i < users.size(); i++) {
        //     std::string uname = users[i].username;
        //     reply->add_all_users(uname.c_str());
        //     if (uname == request->username()) {
        //         this_user = &users[i];
        //     }
        // }
        // // Check if username not found in users
        // if (this_user == nullptr) {
        //     reply->set_msg(FAILURE_INVALID_USERNAME);
        //     return Status::OK;
        // }
        // // Copy following users
        // for (int i = 0; i < this_user->followers.size(); i++) {
        //     reply->add_following_users(this_user->followers[i].c_str());
        // }
        // reply->set_msg(SUCCESS);
        // return Status::OK;
    }

    // RE(!) Huge non datastore shortcut to debug other componenets
    Status Follow(ServerContext* context, const Request* request, Reply* reply) override {
        // (!)(!)(!) Refactor to use datastore, try different SyncService refresh frequencies
        /*
            Propogate coordinator response to client

            HACKY: Without our sync service being called at higher frequency, we have to
            go to the coordinator for up to date info.

            Send FollowUpdate(user, user_to_follow) to Coordinator
            Coordinator does ClientEntry[user_to_follow].followers += user
            When SyncService checks in the next time it
                UpdatesAllFollowers in memory
                Writes these to all .../$CID/followers.data
        */

        // * Get follower and followee names
        std::string follower = request->username();
        std::string followee = request->arguments(0);

        // * Make sure we don't try to follow self -- which is already done by default
        if (follower == followee) {
            reply->set_msg("FAILURE_ALREADY_EXISTS");
            return Status::OK;
        }

        // * Issue RPC::FollowUpdate(follower, followee) --- the coordinator will tell us if
        //   a. followee exists
        //   b. followee is not already being followed by follower
        Request req;        // (!) RPC can just take the request parameter instead of copying
        req.set_username(follower);
        req.add_arguments(followee);

        Reply repl;
        ClientContext ctx;
        Status stat = coord_stub_->FollowUpdate(&ctx, req, &repl);

        // reply->set_msg(repl.msg());
        // * Translate pseudo-HTTP error codes to SNS codes
        if (repl.msg() == "200") {
            reply->set_msg("SUCCESS");
        } else if (repl.msg() == "404") {
            reply->set_msg("FAILURE_NOT_EXISTS");
        } else {
            reply->set_msg("FAILURE_UNKNOWN");
        }

        // * Return status of FollowUpdate
        return stat;

        // std::string uname = request->username();
        // std::string uname_to_follow = request->arguments(0);

        // // check if uname_to_follow is in following, if not add it
        // get_user_entry(uname)->push_following(uname_to_follow);

        // // User is trying to follow themselves, which is done automatically on login
        // if (uname_to_follow == uname) {
        //     reply->set_msg(FAILURE_ALREADY_EXISTS);
        //     return Status::OK;
        // }
        // // Check if user is trying to follow one which DNE
        // User* ufollow_entry = get_user_entry(uname_to_follow);
        // if (ufollow_entry == nullptr) {
        //     reply->set_msg(FAILURE_NOT_EXISTS);
        //     return Status::OK;
        // } 
        // // Check if user is trying to follow a user they already follow
        // if (ufollow_entry->is_follower(uname) == -1) {
        //     ufollow_entry->followers.push_back(uname);
        //     reply->set_msg(SUCCESS);
        //     return Status::OK;    
        // }
        // reply->set_msg(FAILURE_ALREADY_EXISTS);
        // return Status::OK;
    }

    Status UnFollow(ServerContext* context, const Request* request, Reply* reply) override {
        

        std::cout << "\nGot unimplemented UnFollow RPC\n\n";
        return Status::OK;
        
        // /* To test:
        //     a. If user tries to unfollow themselves
        //     b. If user tries to unfollow a user they dont
        //     c. If user tries to unfollow a user which doesn't exist
        //     d. If user tries to unfollow a user which does exist
        // */

        // std::string uname = request->username();
        // std::string unfollow_name = request->arguments(0);

        // // check if unfollow_name is in following, if so remove it
        // get_user_entry(uname)->pop_following(unfollow_name);

        // // Check if user tries to unfollow themselves, which we prevent
        // if (uname == unfollow_name) {
        //     reply->set_msg(FAILURE_INVALID_USERNAME);
        //     return Status::OK;
        // }
        // User* ufollow_entry = get_user_entry(unfollow_name);
        // // Check if user tries to follow one which DNE
        // if (ufollow_entry == nullptr) {
        //     reply->set_msg(FAILURE_NOT_EXISTS);
        //     return Status::OK;
        // }
        // // Check if user tries to unfollow one which they don't follow
        // if (ufollow_entry->pop_follower(uname)) {
        //     reply->set_msg(SUCCESS);
        // } else {
        //     reply->set_msg(FAILURE_INVALID_USERNAME);   // Wasn't following
        // }
        // return Status::OK;
    }
  
    Status Login(ServerContext* context, const Request* request, Reply* reply) override {

        // * Init user entry
        User uentry(request->username());
        users.push_back(uentry);

        // * Init client filesystem
        std::string cid_ = request->username();
        SchmokieFS::PrimaryServer::init_client_fs(cluster_sid, cid_);
        return Status::OK;
        
        // // * Check if uname already, use the previous entry in memory
        // std::string uname = request->username();
        // for (int i = 0; i < users.size(); i++) {
        //     if (users[i].username == uname) {
        //         return Status::OK;
        //     }
        // }
        // // * Add to user table and return OK
        // User uentry(uname);
        // users.push_back(uentry);
        // return Status::OK;
    }

    //(!)(!)(!) Must handle returning user case, where we need to send them 20 off the bat, even if they
    //          are marked as 0
    //          SOL: On client disco -> set last 20 data entries as IOflag=1 (to be read)
    Status Timeline(ServerContext* context, ServerReaderWriter<Message, Message>* stream) override {
        /*
            TIMELINE
            if a user connects that already has timeline file, send latest 20 msgs
            else, make a new stream

            when a user sends, write to sent_messages.data (local or global)

            periodically check the .../$CID/timeline.data for new messages and send 
            along stream
        */
        while(true) {
            /* ------- Inbound messages Client->Server->sent_messages.data ------- */
            // * Handle messages inbound on stream and write to .../$SID/sent_messages.data
            Message inbound_msg;
            while (stream->Read(&inbound_msg)) {
                std::string cid = inbound_msg.username();
                std::string msg = inbound_msg.msg();

                // * Handle INIT msg
                if (msg == "INIT") {
                    User* usr = get_user_entry(cid);
                    if (usr == nullptr) {
                        std::cout << "Could not get user entry for cid=" << cid << '\n';//(!)
                        return Status::CANCELLED;
                    }
                    // * Save stream in user table and turn on timeline mode for user
                    usr->set_stream(stream);
                    continue;
                }

                // * Write inbound_msg to .../$SID/primary/sent_messages.data so SyncService can propogate it
                SchmokieFS::PrimaryServer::write_to_sent_msgs(cluster_sid, inbound_msg);
            }

            /* ------- Outbound messages timeline.data->Server->Client ------- */
            // * Check all .../$SID/primary/local_clients/*/timeline.data for new entries
            //   if one exists, forward it to that user
            for (const User& usr_: users) {
                // * Get new messages from timeline.data
                std::vector<Message> new_msgs = SchmokieFS::PrimaryServer::read_new_timeline_msgs(cluster_sid, usr_.username);
                // * Forward all new messages to user, if any
                for (const Message& msg_: new_msgs) {
                    stream->Write(msg_);
                }
            }
        }

        // * Make the compiler happy
        return Status::OK;

        // while (true) {
        //     // * read message from stream
        //     Message recv, send;
        //     while (stream->Read(&recv)) {
        //        // * take username
        //        std::string uname = recv.username();
        //        std::string rmsg = recv.msg();
        //        if (rmsg == "INIT") {
        //            // * find username entry in users table
        //            User* user = get_user_entry(uname);
        //            if (user != nullptr) {
        //                 // * save stream in user table, set timeline mode to true
        //                 user->set_stream(stream);
        //            }
        //            continue; // do not fwd init messages
        //        }
        //         // * for each follower in that user, write a message to their stream
        //         //   iff the stream is open (the follower is in TIMELINE mode)
        //         /*
        //             We're using User::following vector for message routing as such
        //             When user A sends a message
        //               iterate through ALL users in user table
        //                 check if they are following A, if so, forward message

        //             NOTE: User::following is different than the User::followers vector
        //         */
        //         for (int i = 0; i < users.size(); i++) {
        //             // * Check user name for follow
        //             User* u = &users[i];
        //             if (u->is_following(uname)) {
        //                 if (u->stream) {
        //                     // u->stream->Write(send);
        //                     // Forward recv message
        //                     u->stream->Write(recv);
        //                 }
        //             }
        //         }
        //     }
        // }   // * repeat
        // return Status::OK;
    }

    std::string coordinator_addr;
    std::string cluster_sid;
    std::string port;
    ServerType type_at_init;
    std::vector<User> users;

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

	// * Init SchmokieFS server file system
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

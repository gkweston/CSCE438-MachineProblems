/* ------- server ------- */
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
#include <mutex>
#include <thread>

#include "tsn_server.h"
#include "schmokieFS.h"
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
using csce438::Beat;
using csce438::SNSCoordinatorService;

#define DEFAULT_HOST 				(std::string("0.0.0.0"))
#define FILE_DELIM   				(std::string("|:|"))
#define HRTBT_FREQ                  (7000)
#define DEBUG                       (0)

class SNSServiceImpl final : public SNSService::Service {

    Status List(ServerContext* context, const Request* request, Reply* reply) override {
        // Instead of issuing, try reading from mem like we're aiming to do...

        // * Read all from .../$SID/primary/global_clients.data
        std::vector<std::string> glob_clients = schmokieFS::PrimaryServer::read_global_clients(cluster_sid);
        for (const std::string& cid_: glob_clients) {
            reply->add_all_users(cid_);
        }

        // * Read all from .../$CID/followers.data
        // Technically calls a SyncService method here. We would add its own in the real world
        std::vector<std::string> followers = schmokieFS::SyncService::read_followers_by_cid(cluster_sid, request->username(), "primary"); 
        for (const std::string& cid_: followers) {
            reply->add_following_users(cid_);
        }

        reply->set_msg("SUCCESS");

        return Status::OK;
    }
    Status Follow(ServerContext* context, const Request* request, Reply* reply) override {
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
        Request req;
        req.set_username(follower);
        req.add_arguments(followee);

        Reply repl;
        ClientContext ctx;
        Status stat = coord_stub_->FollowUpdate(&ctx, req, &repl);

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
    }
    Status UnFollow(ServerContext* context, const Request* request, Reply* reply) override {

        std::cout << "\nGot unimplemented UnFollow RPC\n\n";
        return Status::OK;

    }
    Status Login(ServerContext* context, const Request* request, Reply* reply) override {
        std::string cid_ = request->username();
        bool isFirst = request->arguments(0) == "first";

        // * Init user entry if it DNE
        User* uptr = get_user_entry(cid_);
        if (uptr == nullptr) {
            // * New user we haven't encountered yet
            User uentry(request->username());
            users.push_back(uentry);
        }

        if (isFirst) {
            // * If this is their first login to us, we want to set their timeline
            //   to unread. This allows old users to join and see previous chats.
            schmokieFS::PrimaryServer::set_timeline_unread(cluster_sid, cid_);
        }

        // * Still init client filesystem in case they didn't have a timeline
        schmokieFS::PrimaryServer::init_client_fs(cluster_sid, cid_);
        
        // * Set status message
        reply->set_msg("SUCCESS");

        return Status::OK;
    }
    Status Timeline(ServerContext* context, ServerReaderWriter<Message, Message>* stream) override {
        /* A single use stream which takes a new client message and sends their forwards */
        
        /* ------- Inbound messages Client->Server->sent_messages.data ------- */
        Message init_msg;
        std::string client_cid;
        stream->Read(&init_msg);
        client_cid = init_msg.username();

        Message inbound_msg;
        stream->Read(&inbound_msg);
        // * Write inbound_msg to .../$SID/primary/sent_messages.data so SyncService can propogate it
        schmokieFS::PrimaryServer::write_to_sent_msgs(cluster_sid, inbound_msg);

        
        /* ------- Outbound messages timeline.data->Server->Client ------- */
        std::vector<Message> new_msgs = schmokieFS::PrimaryServer::read_new_timeline_msgs(cluster_sid, client_cid);
        for (const auto& msg_: new_msgs) {
            if(DEBUG) std::cout << "writing to client: " << msg_.msg() << "\n";
            stream->Write(msg_);
        }
        return Status::OK;
    }

    std::string coordinator_addr;
    std::string cluster_sid;
    std::string port;
    std::vector<User> users;
    // The assigned type this server was spun up as
    ServerType type_at_init;

    // Guards against unlikely race condition at is_active with
    // this main() thread and SendHeartbeat() thread
    std::mutex active_mtx;
    // could be a std::atomic<bool>
    bool is_active;
    
    std::unique_ptr<SNSCoordinatorService::Stub> coord_stub_;
	void RegisterWithCoordinator();
    User* get_user_entry(const std::string& uname);

public:
    void SendHeartbeat();
    void wait_until_primary();
    SNSServiceImpl(std::string coord_addr, std::string p, std::string sid, ServerType t);
};
SNSServiceImpl::SNSServiceImpl(std::string coord_addr, std::string p, std::string sid, ServerType t) {
	// Server descriptors
	coordinator_addr = coord_addr;
	port = p;
	cluster_sid = sid;

	type_at_init = t;
    if (type_at_init == ServerType::PRIMARY) {
        is_active = true;
    } else {
        is_active = false;
    }
	
	// * Generate coordinator stub here so we can reuse
	coord_stub_ = std::unique_ptr<SNSCoordinatorService::Stub>(
		SNSCoordinatorService::NewStub(
			grpc::CreateChannel(
				coordinator_addr, grpc::InsecureChannelCredentials()
			)
		)
	);

	// * Send registration message
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
	if(DEBUG) std::cout << "Registering as type=" << server_type_str << '\n';
	reg.set_type(server_type_str);

	Reply repl;
	ClientContext ctx;

	// * Dispatch registration RPC
	Status stat = coord_stub_->RegisterServer(&ctx, reg, &repl);
	if (!stat.ok()) {
		std::cerr << "Fatal registration error for:\nsid=" << cluster_sid << ", type=" << type_at_init << "\n";
	}

	// * Init schmokieFS server file system
	schmokieFS::PrimaryServer::init_server_fs(cluster_sid);
}
void SNSServiceImpl::SendHeartbeat() {
    if(DEBUG) std::cout << "Sending heartbeat\n";

    Beat send;
    send.set_sid(cluster_sid);
    
    if (type_at_init == ServerType::PRIMARY) {
        send.set_server_type("primary");
    } else {
        send.set_server_type("secondary");
    }
    Beat recv;
    ClientContext ctx;
    Status stat = coord_stub_->Heartbeat(&ctx, send, &recv);

    ServerType type_recv = ServerType::PRIMARY;
    if (recv.server_type() != "primary") {
        type_recv = ServerType::SECONDARY;
    }

    // * If server is secondary and type_recv is secondary, that means this server should
    //   now be active
    if (type_recv == type_at_init) {
        // * This server is now the active one on the cluster
        active_mtx.lock();
        is_active = true;
        active_mtx.unlock();
    } else {
        // * This server is now innactive on the cluster
        active_mtx.lock();
        is_active = false;
        active_mtx.unlock();
    }


    if(DEBUG) {
        std::cout << "End heartbeat, server is ";
        if (is_active) {
            std::cout << "active\n";
        } else {
            std::cout << "inactive\n";
        }
    }
}
void SNSServiceImpl::wait_until_primary() {
    
    while(!is_active) {
        if(DEBUG) std::cout << "Backing up primary files\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(HRTBT_FREQ));
    }

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
	std::string server_address = DEFAULT_HOST + ":" + port_no; 
	SNSServiceImpl service(coord_addr, port_no, sid, type);
	

	ServerBuilder builder;
	builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
	builder.RegisterService(&service);
	std::unique_ptr<Server> server(builder.BuildAndStart());
	std::cout << "Server listening on " << server_address << std::endl;

    std::thread heartbeat([&]() {
        // * Dispatch a thread to send heartbeat every HRTBT_FREQ ms
        while(true) {
            service.SendHeartbeat();
            std::this_thread::sleep_for(std::chrono::milliseconds(HRTBT_FREQ));
        }
    });
    service.wait_until_primary();

	server->Wait();

    heartbeat.join();
}
bool is_numeric(const std::string& s) {
    return !s.empty() &&
        std::find_if(   s.begin(),
                        s.end(),
                        [](unsigned char c) { return !std::isdigit(c); }) == s.end();
}

int main(int argc, char** argv) {

    std::string helper =
        "Calling convention for server:\n\n"
		"./tsn_server -c <coordIP>:<coordPort> -p <serverPort> -i <serverID> -t <primary|secondary>\n\n";

	if (argc == 1) {
		std::cout << helper;
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
                std::cerr << helper;
                return 0;
		}
	}

    if (!is_numeric(serverID)) {
        std::cout << "SID must be a numeric value, exiting\n";
        return 0;
    }
	
	RunServer(coord, serverID, port, type);
	return 0;
}

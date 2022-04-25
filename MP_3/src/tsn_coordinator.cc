/* ------- coordinator ------- */
#include <vector>
#include <thread>
#include <grpc++/grpc++.h>
#include "sns.grpc.pb.h"
#include "tsn_coordinator.h"

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using grpc::ServerReaderWriter;

using csce438::Request;
using csce438::Reply;
using csce438::Assignment;
using csce438::Registration;
using csce438::GlobalUsers;
using csce438::FlaggedDataEntry;
using csce438::Forward;
using csce438::Beat;
using csce438::SNSCoordinatorService;

#define DEFAULT_HOST    (std::string("0.0.0.0"))
#define HRTBT_FREQ      (5000)

// Wasteful, but useful for now.
#define DEBUG           (0)

class SNSCoordinatorServiceImpl final : public SNSCoordinatorService::Service {
private:

    // Useful when adding features to set a static value
    int N_SERVER_CLUSTERS = 0;
    
    std::vector<ServerEntry> server_routing_table;
    std::vector<ClientEntry> client_routing_table;
    
    ServerEntry* get_server_entry(std::string sid);
    ClientEntry* get_client_entry(std::string cid);

public:
    /* --- Server -------------------------------------- */
    Status RegisterServer(ServerContext* ctx, const Registration* reg, Reply* repl) override {
        // We'll recv this RPC on server startup and add to the appropo routing table

        // * Check if the sid exists in server_routing_table
        ServerEntry* serv_entry = get_server_entry(reg->sid());
        ServerType serv_type = parse_type(reg->type());
        
        if (serv_entry != nullptr) {
            // * Server exists, add this server to that one's entry (overwrite primary, append secondary)
            serv_entry->update_entry(reg->port(), serv_type);
        } else {
            // * This is a new server
            ServerEntry s_entry(reg->sid(), reg->hostname(), reg->port(), serv_type);
            server_routing_table.push_back(s_entry);
            ++N_SERVER_CLUSTERS;
        }

        // * Set repl message to success or failure
        repl->set_msg("200");
        return Status::OK;
    }
    Status FollowUpdate(ServerContext* ctx, const Request* req, Reply* repl) override {
        // Kept in mem on the coordinator, these are followers across clusters
        // given that the server can take care of local message routing
        
        // Comes as req.username=cid, req.arguments[0]=user to follow
        std::string follower = req->username();
        std::string followee = req->arguments(0);

        // * Check if followee is in client_routing_table[i].cid
        ClientEntry* cle = get_client_entry(followee);

        // * If not, something is probably wrong, a server is asking to follow a 
        //   client who is not registered with us, return not found
        if (cle == nullptr) {
            if(DEBUG) std::cerr << "ERR ON FOLLOWUPDATE, CLIENT TO FOLLOW NOT FOUND\n";
            repl->set_msg("404");
            return Status::OK;
        }

        // * Add follower to followees followers (and say that 5 times fast!) return OK
        cle->followers.push_back(follower);
        repl->set_msg("200");
        return Status::OK;
    }
    Status Heartbeat(ServerContext* ctx, const Beat* inbound, Beat* outbound) override {
            
        if(DEBUG) std::cout << "heartbeat recv from sid=" << inbound->sid() << ":" << inbound->server_type() << " \n";

        std::chrono::duration<double, std::milli> prim_ms_duration;
        std::chrono::duration<double, std::milli> secd_ms_duration;
        std::chrono::system_clock::time_point recv_time = std::chrono::high_resolution_clock::now();

        // std::string this_sid = inbound->sid();
        std::string this_type = inbound->server_type();

        ServerEntry* serv_entry = get_server_entry(inbound->sid());
        prim_ms_duration = recv_time - serv_entry->primary_last;
        secd_ms_duration = recv_time - serv_entry->secondary_last;

        bool prim_fault = prim_ms_duration.count() > (HRTBT_FREQ * 2);
        bool secd_fault = secd_ms_duration.count() > (HRTBT_FREQ * 2);

        
        if (prim_fault && secd_fault) {
            std::cerr <<    "Something went wrong, both servers offline\n"
                            "please ensure you have spun up a secondary\n"
                            "server before faulting the first.\n";
            
            
        }

        // * If primary has exceeded our duration+window, we need to promote
        //   the secondary for this cluster
        if(prim_fault) {
            if(DEBUG) std::cout << "primary has faulted! t=" << prim_ms_duration.count() << "ms\n";
            serv_entry->promote_secondary();
        }

        // * If the primary came back online, we could demote secondary as well
        // if(secd_fault && serv_entry->primary_status == ServerStatus::ACTIVE) {
        //     serv_entry->demote_secondary();
        // }

        // * Respond with the SID and the active server
        outbound->set_sid(inbound->sid());
        std::string s_active = "primary";
        if (prim_fault) {
            s_active = "secondary";
        }

        outbound->set_server_type(s_active);

        // * Set previously recv'd times here
        serv_entry->heartbeat_timestamp(this_type);

        return Status::OK;
    }
    
    /* --- Client -------------------------------------- */
    Status FetchAssignment(ServerContext* ctx, const Request* req, Assignment* assigned) override {

        // * Take the req'ing cid and return the SID=(CID % 3)+1 from routing table
        std::string cid_str = req->username();
        int cid_int = std::stoi(req->username());
        std::string target_sid = std::to_string((cid_int % N_SERVER_CLUSTERS) + 1);

        if(DEBUG) std::cout << "Assigning cid=" << cid_int << ", sid=" << target_sid << '\n';

        ServerEntry* serv_entry = get_server_entry(target_sid);

        // * If no server was found for the generated SID
        if (serv_entry == nullptr) {
            if(DEBUG) std::cerr << "No server found for\ncid=" << cid_int << "target_sid="<<target_sid <<"\n";
            // handle
            assigned->set_sid(std::string("404"));
            assigned->set_hostname(std::string("404"));
            assigned->set_port(std::string("404"));
            return Status::OK;
        }

        // * Check if client in table
        ClientEntry* cptr = get_client_entry(cid_str);
        if (cptr == nullptr) {
            // * Add CID->ClusterID to global client_routing table, constructor adds client
            //   to it's own followers
            ClientEntry client_entry(cid_str, target_sid);
            client_routing_table.push_back(client_entry);
        } else {
            cptr->sid = target_sid;
        }

        assigned->set_sid(serv_entry->sid);
        assigned->set_hostname(serv_entry->hostname);

        // * If primary is active, assign to that, else secondary
        if (serv_entry->primary_status == ServerStatus::ACTIVE) {
            assigned->set_port(serv_entry->primary_port);
        } else if (serv_entry->secondary_status == ServerStatus::ACTIVE) {
            assigned->set_port(serv_entry->secondary_port);
        } else {
            std::cout << "NO ACTIVE SERVER FOR REQD:\nsid=" << serv_entry->sid << '\n';
        }
        // * Finally, add this client to that server's clients_served
        serv_entry->clients_served.push_back(cid_str);
        return Status::OK;
    }

    /* --- SyncService --------------------------------- */
    Status RegisterSyncService (ServerContext* ctx, const Registration* reg, Reply* repl) override {
        // * Find entry
        ServerEntry* serv_entry = get_server_entry(reg->sid());

        // * If entry DNE, reply with a "CLUSTER DNE TRY AGAIN" msg
        if (serv_entry == nullptr) {
            if(DEBUG) std::cerr << "Got RegisterSyncService on not found cluser_id=" << reg->sid() << "\n";
            repl->set_msg("404");
            return Status::OK;
        }

        // * Update sync port
        serv_entry->sync_port = reg->port();
        if(DEBUG) std::cout << "Registered sync service with sid=" << reg->sid() << " with cluster_id=" << serv_entry->sid << '\n';
        repl->set_msg("200");
        return Status::OK;
    }
	Status FetchGlobalClients(ServerContext* ctx, const Request* req, GlobalUsers* glob) override{
        // * Copy all global users to response
        for (const ClientEntry& e : client_routing_table) {
            glob->add_cid(e.cid);
        }
        return Status::OK;
    }
    Status FetchFollowers(ServerContext* ctx, const Request* req, Reply* repl) override{
        // * Fetch a single clients followers
        ClientEntry* cli_entry = get_client_entry(req->username());
        if (cli_entry == nullptr) {
            if(DEBUG) std::cerr << "ERR RPC FETCHFOLLOWERS UNREGISTERED USER for cid=" << req->username() << "\n";
            return Status::OK;
        }
        for (const std::string& u: cli_entry->followers) {
            repl->add_following_users(u);
        }
        repl->set_msg("200");
        return Status::OK;
    }
	
    Status ForwardEntryStream (ServerContext* ctx, ServerReaderWriter<Forward, Forward>* stream) override {
        // * Parse stream init message which ensures order and gives us SyncService.sid
        std::string sync_sid;
        Forward init_msg;
        stream->Read(&init_msg);
        if (init_msg.cid(0) == "SYNCINIT") {
            // * extract SyncService sid
            sync_sid = init_msg.entry();
        }

        /*
        ------- Handle incoming forwards ---------------------
            We map all given:
                Forward{ followers of user | sent_message }
            To:
                ClientEntry -> forwards(sent_message)
        */

        // * Read in Forward{ followers_of_user | sent_message }
        Forward inbound_fwd;
        while(stream->Read(&inbound_fwd)) {
            
            // * for each follower_cid in followers_of_user:
            std::string fwd_entry = inbound_fwd.entry();
            for (int i = 0; i < inbound_fwd.cid_size(); ++i) {
                std::string follower_cid = inbound_fwd.cid(i);
            
                // * add sent_message to ClientEntry[follower_cid]
                ClientEntry* cli_entry = get_client_entry(follower_cid);
                if (cli_entry == nullptr) {
                    if(DEBUG) std::cerr << "Could not get_client_entry for stream->Read\n\n";
                }
                cli_entry->forwards.push(fwd_entry);

                if(DEBUG) {
                    std::cout << "Got forward: " << fwd_entry << "\n";
                    std::cout << "forwarding to cid=" << cli_entry->cid << "\n\n";
                    std::cout << "cid=" << cli_entry->cid << " has followers:\n";
                    for (const auto& f: cli_entry->followers) {
                        std::cout << f << "\n";
                    }
                    std::cout << "\n";
                }
            }
        }
        
        /*
        ------- Handle outbound forwards ---------------------
            Get ServerEntry[sync_sid].clients_served
            If that client has forwards, write them to sync service
        */

        // Looping this way ensures we only iterate over each client once, which may not
        // seem efficient, but otherwise we would have to fetch them each time

        // * For each client
        for (ClientEntry& recvr_entry: client_routing_table) {
            // * If client is not on sync_serv_sid, or has no forwards, skip
            if (recvr_entry.sid != sync_sid || !recvr_entry.has_forwards()) {
                continue;
            }

            if(DEBUG) {
                std::cout << "Client on sid=" << sync_sid << " has forwards:\n";
                std::queue<std::string> fwd_cpy(recvr_entry.forwards);
                while (!fwd_cpy.empty()) {
                    std::cout << fwd_cpy.front() << "\n";
                    fwd_cpy.pop();
                }
                std::cout << "---\n";
            }

            // * Client is served by this cluster, and has forwards, send them
            while(!recvr_entry.forwards.empty()) {
                Forward outbound_fwd;
                outbound_fwd.add_cid(recvr_entry.cid);
                std::string entry = recvr_entry.pop_next_forward();
                outbound_fwd.set_entry(entry);
                stream->Write(outbound_fwd);
            }
        }
        return Status::OK;        
    }
};
ServerEntry* SNSCoordinatorServiceImpl::get_server_entry(std::string sid) {
    // Return a reference to the relevant table entry
    for (int i = 0; i < server_routing_table.size(); ++i) {
        if (server_routing_table[i].sid == sid) {
            return &server_routing_table[i];
        }
    }
    return nullptr;
}
ClientEntry* SNSCoordinatorServiceImpl::get_client_entry(std::string cid) {
    // Return a reference to the relevant table entry
    for (int i = 0; i < client_routing_table.size(); ++i) {
        if (client_routing_table[i].cid == cid) {
            return &client_routing_table[i];
        }
    }
    return nullptr;
}

void RunServer(std::string port) {
    std::string addr = DEFAULT_HOST + ":" + port;
    SNSCoordinatorServiceImpl service;

    ServerBuilder builder;
    builder.AddListeningPort(addr, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    std::unique_ptr<Server> server(builder.BuildAndStart());
    std::cout << "Server listening on " << addr << '\n';
    
    server->Wait();
}

int main(int argc, char** argv) {

    std::string helper =
        "Calling convention for coordinator:\n\n"
        "./tsn_coordinator -p <port>\n"
        "Optional: -c will clear the datastore\n\n";

    if (argc == 1) {
        std::cout << helper;
        return 0;
    }

    

    std::string port = "3010";
    bool clear_ds = false;
    int opt = 0;
    while ((opt = getopt(argc, argv, "cp:")) != -1) {
        switch (opt) {
            case 'p':
                port = optarg;
                break;
            case 'c':
                clear_ds = true;
                break;
            default:
                std::cerr << "Invalid command line arg\n";
                std::cerr << helper;
                return 0;
        }
    }

    if (clear_ds) {
        std::cout <<
            "-------------------------------------------------\n"
            "New coordinator is online, clearing datastore...\n"
            "-------------------------------------------------\n";
        std::system("rm -r ./datastore/*");
    }

    RunServer(port);
    return 0;
}
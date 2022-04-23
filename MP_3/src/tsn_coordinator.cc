/* ------- coordinator ------- */

/* (!)(!)(!)
Notes:
    Step 1
    * Wait for connection requests from clients
    * Assign server via CID % 3
    * Keep routing tables for
        SID, PORT_primary, STATUS
        SID, PORT_secondary, STATUS
        SID, PORT_sync_service, STATUS
    * Keep user tables for
        CID, STATUS_primary, STATUS_secondary
        

    Step 2
    * Init channel for HRTBT, if <20 seconds (2 cycles) of heartbeats are missed
        Assign secondary server as active
        Send REDIRECT to all clients to connect with this server
    * Init channel for LOCK_ACQUIRE, LOCK_RELEASE and distribute locks on this
    
*/

#include <vector>
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
using csce438::SNSCoordinatorService;

// Userful for debugging, ex: if 2 we only assign users to sid=[1|2]
// but other servers can still be registered and exist
#define N_SERVERS       (1)
#define DEFAULT_HOST    (std::string("0.0.0.0"))

class SNSCoordinatorServiceImpl final : public SNSCoordinatorService::Service {
private:
    
    std::vector<ServerEntry> server_routing_table;
    std::vector<ClientEntry> client_routing_table;
    
    // Tracks CID and a queue of all their messages
    // std::vector<ForwardEntry> forwards_to_send; //---(!) DEPRECATED
    
    ServerEntry* get_server_entry(std::string sid);
    // ForwardEntry* get_forward_entry(std::string cid); // ---(!) DEPRECATED
    ClientEntry* get_client_entry(std::string cid);
    // std::string server_reg_helper(const ServerEntry& e); // ---(!) DEPRECATED

public:
    // gRPC functions all defined here
    
    
    /* --- Server -------------------------------------- */
    Status RegisterServer(ServerContext* ctx, const Registration* reg, Reply* repl) override {
        // We'll recv this RPC on server startup and add to the appropo routing table

        // * Check if the sid exists in server_routing_table
        ServerEntry* serv_entry = get_server_entry(reg->sid());
        ServerType serv_type = parse_type(reg->type());

        // * If so, add this server to that one's entry (overwrite primary, append secondary)
        if (serv_entry != nullptr) {
            serv_entry->update_entry(reg->port(), serv_type);
        }

        // * If not, make a new entry
        else {
            ServerEntry s_entry(reg->sid(), reg->hostname(), reg->port(), serv_type);
            server_routing_table.push_back(s_entry);
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
        if (cle == nullptr) { //(!)
            std::cout << "ERR ON FOLLOWUPDATE, CLIENT TO FOLLOW NOT FOUND\n";
            repl->set_msg("404");
            return Status::OK;
        }

        // * Add follower to followees followers (and say that 5 times fast!) return OK
        cle->followers.push_back(follower);
        repl->set_msg("200");
        return Status::OK;
    }
    
    
    /* --- Client -------------------------------------- */
    Status FetchAssignment(ServerContext* ctx, const Request* req, Assignment* assigned) override {

        /*
            (!) TODO: If server DNE, find one that does and assign to that
        */
        
        // * Take the req'ing cid and return the SID=(CID % 3)+1 from routing table
        std::string cid_str = req->username();
        int cid_int = std::stoi(req->username());
        std::string target_sid = std::to_string((cid_int % N_SERVERS) + 1);

        std::cout << "Fetching assignment for\ncid=" << cid_int << "\ntarget_sid=" << target_sid << '\n';//(!)

        ServerEntry* serv_entry = get_server_entry(target_sid);

        // * If no server was found for the generated SID
        if (serv_entry == nullptr) {
            std::cout << "No server found for\ncid=" << cid_int << "target_sid="<<target_sid <<"\n";//(!)
            std::cout <<"Cancelling...\n";//(!)
            // handle
            assigned->set_sid(std::string("404"));
            assigned->set_hostname(std::string("404"));
            assigned->set_port(std::string("404"));
            return Status::OK;
        }

        // * Add CID->ClusterID to global client_routing table
        ClientEntry client_entry(cid_str, target_sid);
        client_routing_table.push_back(client_entry);

        std::cout << "Server assigned for\ncid=" << cid_int << "\nsid=" << target_sid <<'\n';//(!)
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
        // serv_entry->clients_served.push_back(cid_str);
        serv_entry->clients_served.insert(cid_str);
        return Status::OK;
    }


    /* --- SyncService --------------------------------- */
    /* (!) TODO (!) all SyncService RPCs*/
    // Register the sync service, save addr
    Status RegisterSyncService (ServerContext* ctx, const Registration* reg, Reply* repl) override {
        // * Find entry
        ServerEntry* serv_entry = get_server_entry(reg->sid());

        // * If entry DNE, reply with a "CLUSTER DNE TRY AGAIN" msg
        if (serv_entry == nullptr) {
            std::cout << "Got RegisterSyncService on not found cluser_id=" << reg->sid() << ", rejecting\n";//(!)
            repl->set_msg("404");
            return Status::OK;
        }

        // * Update sync port
        serv_entry->sync_port = reg->port();
        std::cout << "Registered sync service with sid=" << reg->sid() << " with cluster_id=" << serv_entry->sid << '\n';
        repl->set_msg("200");
        return Status::OK;
    }
	// Respond with all registered users; returns GlobalUsers
	Status FetchGlobalClients(ServerContext* ctx, const Request* req, GlobalUsers* glob) override{
        if (req->username() != "SYNC") { //(!)
            std::cout << "ERR RPC UNAME FETCHGLOBAL\n";//(!)
        }//(!)
        
        // * Copy all global users to response
        for (const ClientEntry& e : client_routing_table) {
            glob->add_cid(e.cid);
        }
        return Status::OK;
    }
	
    // Send message forwards from Coord to SyncService; Returns stream of messages
    // RE(!) needs to support the new client_routing_table
    // RE(!) probably want to thread this (!)
    Status ForwardEntryStream (ServerContext* ctx, ServerReaderWriter<FlaggedDataEntry, FlaggedDataEntry>* stream) override {
        /*
            Forwards are not necessarily ordered by Incoming, Outbound on the SyncService side, this is ok
            as long as we ensure the first FlaggedDataEntry= { "SYNCINIT", sid }
        */

        // --- Incoming forwards ---v
        // * Recv msg from SyncService to init stream, extract SyncService.sid and get ServerEntry
        std::string sync_sid;
        ServerEntry* sync_serv_entry;
        FlaggedDataEntry init_msg;
        stream->Read(&init_msg);
        if (init_msg.cid() == "SYNCINIT") { // (!) good
            sync_sid = init_msg.entry();
            sync_serv_entry = get_server_entry(sync_sid);
        } else { // (!) bad - should never happen
            std::cout << "ERR UNORDERED INIT MESSAGE ON FORWARDENTRYSTREAM\n\n";
        }

        // * For each msg forward from sync service
        FlaggedDataEntry inbound_fwd;
        while (stream->Read(&inbound_fwd)) {
            // ex: { "222", "1|:|TIME|:|222|:|Hello, it's me!"" }
            // -> originates from SID serving 222
            // -> Should be propogated to all SIDs serving a client following 222

            /// <<<------- Untested thusfar(!)
            // * Get sender entry so we can access their followers
            ClientEntry* sender_entry = get_client_entry(inbound_fwd.cid());
            if (sender_entry == nullptr) { //(!) bad - should never happen
                std::cout << "ERR RECVD UNREGISTERED CLIENT FORWARD FORWARDENTRYSTREAM cid=" << inbound_fwd.cid() << '\n';//(!)
            }
            
            // * Find all servers where there is an intersection of sender.followers and server.clients_served
            //   Iterate over server entries, if they serve a follower of sender, add this entry to their forwards
            for (ServerEntry& s: server_routing_table) {
                // Skip entry for SyncService server_entry
                if (s == *sync_serv_entry) {
                    continue;
                }
                // * Check if this server serves a follower of the sender, if so add to foward queue
                if (s.is_serving_user_from_vec(sender_entry->followers)) {
                    s.forward_queue.push(inbound_fwd);
                }
            }
            /// >>>------- Untested thusfar
        }
        
        // --- Outbound forwards ---v
        // * While ServerEntry::sid==SyncService.sid forward_queue is not empty, send FlaggedDataEntry
        // (!) this may be expensive indirection, expensive enough to just copy the queue and clear it in
        //     the server entry
        while (!sync_serv_entry->forward_queue.empty()) {
            stream->Write(sync_serv_entry->forward_queue.front());
            sync_serv_entry->forward_queue.pop();
        }

        // * Stop sending, let SyncService close stream
        return Status::OK;
    }
    //<<<(!)
};
// RE(!) Could change these to auto for loops, but idk what ownership
//       principles look like for those...
ServerEntry* SNSCoordinatorServiceImpl::get_server_entry(std::string sid) {
    // Return a reference to the relevant table entry
    for (int i = 0; i < server_routing_table.size(); ++i) {
        if (server_routing_table[i].sid == sid) {
            return &server_routing_table[i];
        }
    }
    return nullptr;
}
// ForwardEntry* SNSCoordinatorServiceImpl::get_forward_entry(std::string cid) { // ---(!) DEPRECATED
//     // Return a reference to the relevant table entry
//     for (int i = 0; i < forwards_to_send.size(); ++i) {
//         if (forwards_to_send[i].cid == cid) {
//             return &forwards_to_send[i];
//         }
//     }
//     return nullptr;
// }
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

    /*
    Simplified args:
        -p <coordinatorPort>
    */

    if (argc == 1) {//(!)
        std::cout << "Calling convention for coordinator:\n\n";
        std::cout << "./tsn_coordinator -p <port>\n\n";
        return 0;
    }

    // (!)(!)
    std::cout << "\nWARNING: Is ./datastore/* clear??\n\n";// (!)(!)
    // (!)(!)

    std::string port = "3010";
    int opt = 0;
    while ((opt = getopt(argc, argv, "p:")) != -1) {
        switch (opt) {
            case 'p':
                port = optarg;
                break;
            default:
                std::cerr << "Invalid command line arg\n";
        }
    }
    RunServer(port);
    return 0;
}
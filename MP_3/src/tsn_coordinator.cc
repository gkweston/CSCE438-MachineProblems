/* ------- coordinator ------- */
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
using csce438::Forward;
using csce438::SNSCoordinatorService;

// Userful for debugging, ex: if 2 we only assign users to sid=[1|2]
// but other servers can still be registered and exist
#define N_SERVERS       (2)
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
            (!) TODO:
                - If server DNE, find one that does and assign to that
                - On network partition, it may be useful to have client timeout
                  and simply call this method again (requires a refactor)

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

        // * Add CID->ClusterID to global client_routing table, constructor adds client
        //   to it's own followers
        ClientEntry client_entry(cid_str, target_sid);

        client_routing_table.push_back(client_entry);

        std::cout << "Server assigned for\ncid=" << cid_int << "\nsid=" << target_sid <<'\n';//(!)
        assigned->set_sid(serv_entry->sid);
        assigned->set_hostname(serv_entry->hostname);

        /* RE(!) REFACTOR ON FAULT TOLERANCE STEP RE(!)*/
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
	Status FetchGlobalClients(ServerContext* ctx, const Request* req, GlobalUsers* glob) override{
                
        //(!)-------------------------------------------------(!)
        std::cout << "FetchGlobalClients from cid=" << req->username();
        if (req->arguments_size() > 0) {
            std::cout << " origin=" << req->arguments(0);
        }
        std::cout << "\n";
        //(!)-------------------------------------------------(!)


        // RE(!) in final we will only take from SYNC
        // if (req->username() != "SYNC") {
        //     std::cout << "ERR RPC UNAME FETCHGLOBAL\n";
        // }
        
        // * Copy all global users to response
        for (const ClientEntry& e : client_routing_table) {
            glob->add_cid(e.cid);
        }
        return Status::OK;
    }
    Status FetchFollowers(ServerContext* ctx, const Request* req, Reply* repl) override{

        //(!)-------------------------------------------------(!)
        std::cout << "FetchFollowers for cid=" << req->username();
        if (req->arguments_size() > 0) {
            std::cout << " origin=" << req->arguments(0);
        }
        std::cout << "\n";
        //(!)-------------------------------------------------(!)

        ClientEntry* cli_entry = get_client_entry(req->username());
        if (cli_entry == nullptr) {
            std::cout << "ERR RPC FETCHFOLLOWERS UNREGISTERED USER\n";//(!)
            return Status::OK;
        }
        for (const std::string& u: cli_entry->followers) {
            repl->add_following_users(u);
        }
        repl->set_msg("200");
        return Status::OK;
    }
	
    // Send message forwards from Coord to SyncService; Returns stream of messages
    /*RE(!)

        Inbound:
        Read in Forward{ followers_of_user | sent_message }
        for each follower_cid in followers_of_user:
            add sent_message to ClientEntry[follower_cid]

        
        Outbound:
        for each served_cid in ServerEntry[SyncService.sid].clients_served
            if ClientEntry[served_cid] has forwards:
                send Forward{ served_cid | sent_message }
    */ 
    Status ForwardEntryStream (ServerContext* ctx, ServerReaderWriter<Forward, Forward>* stream) override {
        // * Parse stream init message which ensures order and gives us SyncService.sid
        std::string sync_sid;
        Forward init_msg;
        stream->Read(&init_msg);
        if (init_msg.cid(0) == "SYNCINIT") { // (!) good
            // * extract SyncService sid
            sync_sid = init_msg.entry();
        } else { // (!) bad - should never happen
            std::cout << "ERR UNORDERED INIT MESSAGE ON FORWARDENTRYSTREAM\n\n";
        }

        /*
        ------- Handle incoming forwards ---------------------

            We map all give
                Forward{ followers of user | sent_message }
            To
                ClientEntry -> forwards(sent_message)

        */

        // * Read in Forward{ followers_of_user | sent_message }
        Forward inbound_fwd;
        while(stream->Read(&inbound_fwd)) {
            // * for each follower_cid in followers_of_user:
            std::string fwd_entry = inbound_fwd.entry();
            for (int i = 0; i < inbound_fwd.cid_size(); ++i) {
                std::string follower_cid = inbound_fwd.cid(0);
                // * add sent_message to ClientEntry[follower_cid]
                ClientEntry* cli_entry = get_client_entry(follower_cid);
                cli_entry->forwards.push(fwd_entry);


                //(!)-------------------------------------------------(!)
                std::cout << "Got forward: " << fwd_entry << "\n";
                std::cout << "Giving it to cid=" << cli_entry->cid << "\n\n";
                //(!)-------------------------------------------------(!)

            }
        }

        
        /*
        ------- Handle outbound forwards ---------------------

            Get ServerEntry[sync_sid].clients_served
            If that client has forwards, write them to sync service

            (!) add a has_forwards to clients_served to save on indirection cost

        */
        // * For each clients_served
        ServerEntry* sync_serv_entry = get_server_entry(sync_sid);
        for (const std::string& recvr_cid: sync_serv_entry->clients_served) {

            // * Get client entry and check if they have forwards
            ClientEntry* cli_entry = get_client_entry(recvr_cid);
            if (cli_entry == nullptr) {
                std::cout << "ERR OUTBOUND FORWARDS NO CLIENT FOR cid=" << recvr_cid << '\n';//(!)
                std::cout << "CANCELLING STREAM\n";//(!)
                return Status::CANCELLED;//(!)
            }

            // * If so, write those to SyncService
            Forward outbound_fwd;
            outbound_fwd.add_cid(recvr_cid);
            while(cli_entry->has_forwards()) {

                //(!)-------------------------------------------------(!)
                std::cout << recvr_cid << ".forwards.size=" << cli_entry->forwards.size() << "\n";
                //(!)-------------------------------------------------(!)

                outbound_fwd.set_entry(cli_entry->forwards.front());

                //(!)-------------------------------------------------(!)
                std::cout << "Have outbound forward from cid=" << cli_entry->cid << "\n";
                std::cout << "Forwarding message: " << cli_entry->forwards.front() << "\n";
                //(!)-------------------------------------------------(!)

                cli_entry->forwards.pop();
                stream->Write(outbound_fwd);
            }
        }

        return Status::OK;        
    }
    // >>>(!)
    // Status ForwardEntryStream (ServerContext* ctx, ServerReaderWriter<FlaggedDataEntry, FlaggedDataEntry>* stream) override {
    //     /*
    //         Forwards are not necessarily ordered by Incoming, Outbound on the SyncService side, this is ok
    //         as long as we ensure the first FlaggedDataEntry= { "SYNCINIT", sid }
    //     */

    //     // --- Incoming forwards ---v
    //     // * Recv msg from SyncService to init stream, extract SyncService.sid and get ServerEntry
    //     std::string sync_sid;
    //     ServerEntry* sync_serv_entry;
    //     FlaggedDataEntry init_msg;
    //     stream->Read(&init_msg);
    //     if (init_msg.cid() == "SYNCINIT") { // (!) good
    //         sync_sid = init_msg.entry();
    //         sync_serv_entry = get_server_entry(sync_sid);
    //     } else { // (!) bad - should never happen
    //         std::cout << "ERR UNORDERED INIT MESSAGE ON FORWARDENTRYSTREAM\n\n";
    //     }

    //     // * For each msg forward from sync service
    //     FlaggedDataEntry inbound_fwd;
    //     while (stream->Read(&inbound_fwd)) {
    //         // ex: { "222", "1|:|TIME|:|222|:|Hello, it's me!"" }
    //         // -> originates from SID serving 222
    //         // -> Should be propogated to all SIDs serving a client following 222

    //         /// <<<------- Untested thusfar(!)
    //         // * Get sender entry so we can access their followers
    //         ClientEntry* sender_entry = get_client_entry(inbound_fwd.cid());
    //         if (sender_entry == nullptr) { //(!) bad - should never happen
    //             std::cout << "ERR RECVD UNREGISTERED CLIENT FORWARD FORWARDENTRYSTREAM cid=" << inbound_fwd.cid() << '\n';//(!)
    //         }
            
    //         // * Find all servers where there is an intersection of sender.followers and server.clients_served
    //         //   Iterate over server entries, if they serve a follower of sender, add this entry to their forwards
    //         for (ServerEntry& s: server_routing_table) {
    //             // Skip entry for SyncService server_entry
    //             if (s == *sync_serv_entry) {
    //                 continue;
    //             }
    //             // * Check if this server serves a follower of the sender, if so add to foward queue
    //             if (s.is_serving_user_from_vec(sender_entry->followers)) {
    //                 s.forward_queue.push(inbound_fwd);
    //             }
    //         }
    //         /// >>>------- Untested thusfar
    //     }
        
    //     // --- Outbound forwards ---v
    //     // * While ServerEntry::sid==SyncService.sid forward_queue is not empty, send FlaggedDataEntry
    //     // (!) this may be expensive indirection, expensive enough to just copy the queue and clear it in
    //     //     the server entry
    //     while (!sync_serv_entry->forward_queue.empty()) {
    //         stream->Write(sync_serv_entry->forward_queue.front());
    //         sync_serv_entry->forward_queue.pop();
    //     }

    //     // * Stop sending, let SyncService close stream
    //     return Status::OK;
    // }
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
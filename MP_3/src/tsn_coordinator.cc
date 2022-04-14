/*
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
#include "coordinator.h"

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
using csce438::UnflaggedDataEntry;
using csce438::SNSCoordinatorService;

#define N_SERVERS (1)
#define DEFAULT_HOST (std::string("0.0.0.0"))

class SNSCoordinatorServiceImpl final : public SNSCoordinatorService::Service {
private:
    
    std::vector<ServerEntry> server_routing_table;
    std::vector<ClientEntry> client_routing_table;
    std::vector<ForwardEntry> forwards_to_send;
    
    ServerEntry* get_server_entry(std::string sid);
    ForwardEntry* get_forward_entry(std::string cid);
    // std::string server_reg_helper(const ServerEntry& e); //(!)

public:
    // Only gRPC functions defined inline
    // --- Server RPCs
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
        repl->set_msg("SERVER REGISTERED");
        return Status::OK;
    }
    // --- Client RPCs
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
        serv_entry->clients_served.push_back(cid_str);

        return Status::OK;
    }

    // --- Sync service RPCs
    /* (!) TODO (!) all SyncService RPCs*/
    // Register the sync service, save addr
    Status RegisterSyncService (ServerContext* ctx, const Registration* reg, Reply* repl) override {

        // * Find entry
        ServerEntry* serv_entry = get_server_entry(reg->sid());

        // * If entry DNE, reply with a "CLUSTER DNE TRY AGAIN" msg
        if (serv_entry == nullptr) {
            repl->set_msg("404");
            return Status::OK;
        }

        // * Update sync port
        serv_entry->sync_port = reg->port();
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
    // (?)(!) cheaper just to do a unidirectional repeated msg??
    // RE(!) probably want to thread this (!)
	Status ForwardEntryStream (ServerContext* ctx, ServerReaderWriter<UnflaggedDataEntry, UnflaggedDataEntry>* stream) override {
        
        // * Read special init message and set save sid
        std::string sync_serv_sid;
        UnflaggedDataEntry init_msg;
        stream->Read(&init_msg);

        if (init_msg.cid() == "SYNCINIT") {
            // Save this so we know which messages (if any) to forward after
            // reading their entries
            sync_serv_sid = init_msg.entry();
        }

        // --- Start handling incoming forwards
        // * Read all incoming forwards from sync service
        UnflaggedDataEntry incoming_fwd;
        ForwardEntry* fwd_entry = nullptr;
        std::string prev_cid = "";
        while (stream->Read(&incoming_fwd)) {

            /*(!)
            forwards_to_send looks like:
                string CID, queue {TIME|:|CID|:|MSG}

                we store CID twice so if we have issues we can debug
                then if it's working we may remove the doublestore
            */

            // Check if we already found the index of this cid
            std::string curr_cid = incoming_fwd.cid();
            if (prev_cid != curr_cid) {
                // Got a different cid, find the index if exists
                // idx = get_idx_forward_entry(curr_cid);
                fwd_entry = get_forward_entry(curr_cid);
            }

            // Check if we have a container for these forwards already
            
            // * Is there a forward container for this cid?
            if (fwd_entry != nullptr) {
                // exists
                // * If so, get this container and add forward
                // forwards_to_send[idx].data_entries.push(incoming_fwd);
                fwd_entry->data_entries.push(incoming_fwd);
            } else {
                // DNE
                // * Else, create new container
                ForwardEntry new_fwd_e;
                new_fwd_e.cid = curr_cid;
                new_fwd_e.data_entries.push(incoming_fwd);
                forwards_to_send.push_back(new_fwd_e);
                
                // save idx of this entry in case the next has the same cid
                
                // there may be copy semantics when we push to vector, so to be
                // safe we will get a pointer to the element we just pushed
                // idx = forwards_to_send.size();
                fwd_entry = &forwards_to_send[forwards_to_send.size() - 1];
            }
            // save the prev cid
            prev_cid = curr_cid;
        }
        // --- All done with incoming forwards
        // * Does THIS sync service serve any clients we have forwards for?
        ServerEntry* serv_entry = get_server_entry(sync_serv_sid);

        for (const std::string& client_id: serv_entry->clients_served) {
            // Check if this client has any data entry forwards
            fwd_entry = get_forward_entry(client_id);

            if (fwd_entry == nullptr) {
                // No forwards here, move along...
                return Status::OK;
            }

            // * If the connected sync services this client and this client has forwards, send them
            //   to sync and remove fwds from memory
            while(!fwd_entry->data_entries.empty()) {
                stream->Write(fwd_entry->data_entries.front());
                fwd_entry->data_entries.pop();
            }

        }
        // * Finish stream
        return Status::OK;
    }
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
ForwardEntry* SNSCoordinatorServiceImpl::get_forward_entry(std::string cid) {
    // Return a reference to the relevant table entry
    for (int i = 0; i < forwards_to_send.size(); ++i) {
        if (forwards_to_send[i].cid == cid) {
            return &forwards_to_send[i];
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
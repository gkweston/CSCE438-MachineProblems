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
    
    int get_idx_server_entry(std::string sid);
    // std::string server_reg_helper(const ServerEntry& e); //(!)

public:
    // Only gRPC functions defined inline
    // --- Server RPCs
    Status RegisterServer(ServerContext* ctx, const Registration* reg, Reply* repl) override {
        // We'll recv this RPC on server startup and add to the appropo routing table

        // * Check if the sid exists in server_routing_table
        int idx = get_idx_server_entry(reg->sid());
        ServerType serv_type = parse_type(reg->type());

        // * If so, add this server to that one's entry (overwrite primary, append secondary)
        if (idx != -1) {
            ServerEntry serv_entry = server_routing_table[idx];
            serv_entry.update_entry(reg->port(), serv_type);
        }

        // * If not, make a new entry
        else {
            ServerEntry s_entry(reg->sid(), reg->hostname(), reg->port(), serv_type);
            server_routing_table.push_back(s_entry);
        }

        // * Set repl message to success or failure
        repl->set_msg("SERVER REGISTERED");
        return Status::OK;

        // ------->>> DIFF
        // ServerEntry serv;
        
        // // Set server_entry: sid, hostname, port, status, type
        // serv.sid = reg->sid();
        // serv.hostname = reg->hostname();
        // serv.port = reg->port();
        // serv.type = parse_type(std::string(reg->type()));

        // // Flip this to inactive when heartbeat fails
        // serv.status = ServerStatus::ACTIVE;

        // std::string reg_result = server_reg_helper(serv);
        // repl->set_msg(reg_result);
        // return Status::OK;
        // <<<-------
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

        int serv_idx = get_idx_server_entry(target_sid);

        // * If no server was found for the generated CID
        if (serv_idx == -1) {
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

        // (!) Copy constructor? -- no owned pointers in class, probably ok...
        ServerEntry serv_entry = server_routing_table[serv_idx];
        assigned->set_sid(serv_entry.sid);
        assigned->set_hostname(serv_entry.hostname);

        // * If primary is active, assign to that, else secondary
        if (serv_entry.primary_status == ServerStatus::ACTIVE) {
            assigned->set_port(serv_entry.primary_port);
        } else if (serv_entry.secondary_status == ServerStatus::ACTIVE) {
            assigned->set_port(serv_entry.secondary_port);
        } else {
            std::cout << "NO ACTIVE SERVER FOR REQD:\nsid=" << serv_entry.sid << '\n';
        }
        return Status::OK;
    }

    // --- Sync service RPCs
    /* (!) TODO (!) all SyncService RPCs*/
    // Register the sync service, save addr
    Status RegisterSyncService (ServerContext* ctx, const Registration* reg, Reply* repl) override {
        // Takes Registration, returns Reply

        // * Find entry
        int serv_idx = get_idx_server_entry(reg->sid());

        // * If entry DNE, reply with a "CLUSTER DNE TRY AGAIN" msg
        if (serv_idx == -1) {
            repl->set_msg("404");
            return Status::OK;
        }

        // * Update sync port
        server_routing_table[serv_idx].sync_port = reg->port();
        repl->set_msg("200");
        return Status::OK;
    }
	// Respond with all registered users; returns GlobalUsers
	Status FetchGlobalClients(ServerContext* ctx, const Request* req, GlobalUsers* glob) override{
        // Takes Request, returns GlobalUsers
        std::cout << "not done\n";//(!)
        return Status::CANCELLED;
    }
	// Send message forwards from Coord to SyncService; Returns stream of messages
    // (?)(!) cheaper just to do a unidirectional repeated msg??
	Status Forward (ServerContext* ctx, ServerReaderWriter<UnflaggedDataEntry, UnflaggedDataEntry>* stream) override {
        // Takes stream Message, returns stream Message
        return Status::CANCELLED;
    }
};
// ------->>> DIFF
// std::string SNSCoordinatorServiceImpl::server_reg_helper(const ServerEntry& e) {
//     // Check if this server already exists in the routing table, if so, replace with new entry
//     // std::vector<ServerEntry>* tbl = &[primary_routing_table|secondary_routing_table]
//     int idx;
//     if (e.type == ServerType::PRIMARY) {
//         idx = find_primary_server(e.sid);

//         if (idx < 0) { //DNE
//             primary_routing_table.push_back(e);
//             std::cout << "Registered primary server\nsid=" << e.sid << "\nport=" << e.port << "\ntype=PRIMARY" << "\n";//(!)
//         } else { // Overwrite
//             primary_routing_table[idx] = e;
//             std::cout << "Registered primary server overwrite\nsid=" << e.sid << "\nport=" << e.port << "\ntype=PRIMARY" << "\n";//(!)
//         }
//     } else if (e.type == ServerType::SECONDARY) {
//         idx = find_secondary_server(e.sid);

//         if (idx < 0) { //DNE
//             secondary_routing_table.push_back(e);
//             std::cout << "Registered secondary server\nsid=" << e.sid << "\nport=" << e.port << "\ntype=SECONDARY" << "\n";//(!)
//         } else { // Overwrite
//             secondary_routing_table[idx] = e;
//             std::cout << "Registered secondary server overwrite\nsid=" << e.sid << "\nport=" << e.port << "\ntype=SECONDARY" << "\n";//(!)
//         }
//     }
//     return std::string("Server Registered");
// }
// int SNSCoordinatorServiceImpl::find_primary_server(std::string sid) {
//     // For now we return the index of the server or -1
//     int idx = 0;
//     for (ServerEntry e: primary_routing_table) {
//         if (sid == e.sid) {
//             return idx;
//         }
//         ++idx;
//     }
//     return -1;
// }
// int SNSCoordinatorServiceImpl::find_secondary_server(std::string sid) {
//     // For now we return the index of the server or -1
//     int idx = 0;
//     for (ServerEntry e: secondary_routing_table) {
//         if (sid == e.sid) {
//             return idx;
//         }
//         ++idx;
//     }
//     return -1;
// }
// <<<-------
int SNSCoordinatorServiceImpl::get_idx_server_entry(std::string sid) {
    int idx = 0;
    for (ServerEntry e: server_routing_table) {
        if (sid == e.sid) {
            return idx;
        }
        ++idx;
    }
    return -1;
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
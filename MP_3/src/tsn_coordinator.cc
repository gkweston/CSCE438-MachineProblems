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
using csce438::Request;
using csce438::Reply;
using csce438::Assignment;
using csce438::Registration;
using csce438::SNSCoordinatorService;

#define N_SERVERS (1)
#define DEFAULT_HOST (std::string("0.0.0.0"))

class SNSCoordinatorServiceImpl final : public SNSCoordinatorService::Service {
    
    // Only gRPC functions defined inline

    Status RegisterServer(ServerContext* ctx, const Registration* reg, Reply* repl) override {
        // We'll recv this RPC on server startup and add to the appropo routing table
        ServerEntry serv;
        
        // Set server_entry: sid, hostname, port, status, type
        serv.sid = reg->sid();
        serv.hostname = reg->hostname();
        serv.port = reg->port();
        serv.type = parse_type(std::string(reg->type()));
        serv.status = ServerStatus::ACTIVE;

        repl->set_msg(registration_helper(serv));
        return Status::OK;
    }

    Status FetchAssignment(ServerContext* ctx, const Request* req, Assignment* assigned) override {
        
        // Take the req'ing cid and return the SID=(CID % 3)+1 from routing table
        int cid = std::stoi(req->username());
        std::string target_sid = std::to_string((cid % N_SERVERS) + 1);

        std::cout << "Fetching assignment for\ncid=" << cid << "\ntarget_sid=" << target_sid << '\n';//(!)

        // (!) TODO prim and sec, for now just use prim (!)
        // Check routing tables for server
        //      sid == target_sid && status == ACTIVE
        int server_idx = find_primary_server(target_sid);
        // (!) end TODO (!)

        if (server_idx == -1) {
            std::cout << "No server found for\ncid=" << cid << "target_sid="<<target_sid <<"\n";//(!)
            std::cout <<"Cancelling...\n";//(!)
            // handle
            assigned->set_sid(std::string("NONE"));
            assigned->set_hostname(std::string("NONE"));
            assigned->set_port(std::string("NONE"));
            return Status::OK;
        }

        std::cout << "Server assigned for\ncid=" << cid << "\nsid=" << target_sid <<'\n';//(!)
        assigned->set_hostname(primary_routing_table[server_idx].hostname);
        assigned->set_port(primary_routing_table[server_idx].port);
        return Status::OK;
    }

private:
    std::vector<ServerEntry> primary_routing_table;
    std::vector<ServerEntry> secondary_routing_table;

    int find_primary_server(std::string sid);
    int find_secondary_server(std::string sid);
    std::string registration_helper(const ServerEntry& e);
};

std::string SNSCoordinatorServiceImpl::registration_helper(const ServerEntry& e) {
    // Check if this server already exists in the routing table, if so, replace with new entry
    // std::vector<ServerEntry>* tbl = &[primary_routing_table|secondary_routing_table]
    int idx;
    if (e.type == ServerType::PRIMARY) {
        idx = find_primary_server(e.sid);

        if (idx < 0) { //DNE
            primary_routing_table.push_back(e);
            std::cout << "Registered primary server\nsid=" << e.sid << "\nport=" << e.port << "\ntype=PRIMARY" << "\n";//(!)
        } else { // Overwrite
            primary_routing_table[idx] = e;
            std::cout << "Registered primary server overwrite\nsid=" << e.sid << "\nport=" << e.port << "\ntype=PRIMARY" << "\n";//(!)
        }
    } else if (e.type == ServerType::SECONDARY) {
        idx = find_secondary_server(e.sid);

        if (idx < 0) { //DNE
            secondary_routing_table.push_back(e);
            std::cout << "Registered secondary server\nsid=" << e.sid << "\nport=" << e.port << "\ntype=SECONDARY" << "\n";//(!)
        } else { // Overwrite
            secondary_routing_table[idx] = e;
            std::cout << "Registered secondary server overwrite\nsid=" << e.sid << "\nport=" << e.port << "\ntype=SECONDARY" << "\n";//(!)
        }
    }
    return std::string("Server Registered");
}
int SNSCoordinatorServiceImpl::find_primary_server(std::string sid) {
    // For now we return the index of the server or -1
    int idx = 0;
    for (ServerEntry e: primary_routing_table) {
        if (sid == e.sid) {
            return idx;
        }
        ++idx;
    }
    return -1;
}
int SNSCoordinatorServiceImpl::find_secondary_server(std::string sid) {
    // For now we return the index of the server or -1
    int idx = 0;
    for (ServerEntry e: secondary_routing_table) {
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
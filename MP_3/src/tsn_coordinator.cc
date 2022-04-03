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

#define N_SERVERS (3)
#define DEFAULT_HOST (std::string("0.0.0.0"))

// Just one in this iteration, when we intro fault tolerance, we will have another
std::vector<ServerEntry> server_routing_table;

int find_server(std::string sid) {
    // for (int i = 0; i < server_routing_table.size(); ++i) {
    //     if (server_routing_table[i].sid == sid) {
    //         return i;
    //     }
    // }
    // return -1;
    int idx = 0;
    for (ServerEntry e: server_routing_table) {
        if (sid == e.sid) {
            return idx;
        }
        ++idx;
    }
    return -1;
}

class SNSCoordinatorServiceImpl final : public SNSCoordinatorService::Service {

    Status RegisterServer(ServerContext* ctx, const Registration* reg, Reply* repl) override {
        // We'll recv this RPC on server startup and add to the appropo routing table
        ServerEntry serv;

        // Set server_entry: sid, hostname, port, status, type
        serv.sid = reg->sid();
        serv.hostname = reg->hostname();
        serv.port = reg->port();
        serv.type = parse_type(std::string(reg->type()));
        serv.status = ServerStatus::ACTIVE;

        // Reply
        server_routing_table.push_back(serv);
        std::cout << "Registered server\nsid=" << serv.sid << "\nport=" << serv.port << "\ntype=" << reg->type() << "\n";//(!)

        std::string msg = "Server Registered";
        repl->set_msg(msg);
        return Status::OK;
    }

    Status FetchAssignment(ServerContext* ctx, const Request* req, Assignment* assigned) override {
        // Take the req'ing cid and return the SID=(CID % 3)+1 from routing table
        int cid = std::stoi(req->username());
        std::string target_sid = std::to_string((cid % N_SERVERS) + 1);
        int server_idx = find_server(target_sid);

        if (server_idx == -1) {
            std::cout << "No server found for\ncid=" << cid << "target_sid="<<target_sid <<"\n";//(!)
            std::cout <<"Cancelling...\n";//(!)
            // handle
            assigned->set_sid(std::string("NONE"));
            assigned->set_hostname(std::string("NONE"));
            assigned->set_port(std::string("NONE"));
            return Status::CANCELLED;
        }

        std::cout << "Server assigned for\ncid=" << cid << "sid=" << target_sid <<'\n';//(!)
        assigned->set_hostname(server_routing_table[server_idx].hostname);
        assigned->set_port(server_routing_table[server_idx].port);
        return Status::OK;
    }
};

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
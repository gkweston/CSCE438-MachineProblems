/*
    Start this by reading in all messages for ${UID}.txt
    keep { TIME | UID | MSG } in memory

    we'll keep this in memory until it's sent on the wire

    use stat(), or similar, to check when to read in mem

    keep TIME last_data_diff on hand

    VERY sloppy, hacky, needs polishing all over
*/

// (!) New flag (X)=Mutual Exclusion may be necessary

#include <chrono>
#include <thread>
#include <ctime>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <iomanip>
#include <sstream>
#include <queue>

// #include "tsn_database.h"
#include <grpc++/grpc++.h>
#include <google/protobuf/util/time_util.h>
#include "sns.grpc.pb.h"

using google::protobuf::Timestamp;
using grpc::Channel;
using grpc::Status;
using grpc::ClientContext;
using grpc::ClientReader;
using grpc::ClientReaderWriter;
using grpc::ClientWriter;
using grpc::Channel;

using csce438::Message;
using csce438::Request;
using csce438::GlobalUsers;
using csce438::Registration;
using csce438::Reply;
using csce438::UnflaggedDataEntry;
using csce438::SNSCoordinatorService;

#define DEFAULT_HOST (std::string("0.0.0.0"))

/*
    @init:
        Register self with coordinator
        Fetch all global users, write to datastore/$CLUSTER_ID/global_clients.data

    ~Thread1
    1.  Update localclients in memory from
            datastore/$CLUSTER_ID/primary/local_clients.data
    
    2.  For each CID, periodically check
            datastore/$CLUSTER_ID/primary/$CID/sent_messages.data
        for new messages

    3.  For each new_msg send to coordinator for routing

    ~Thread2
    1.  Wait until receipt of new_msg from coordinator

    2.  Update clients from
            datastore/$CLUSTER_ID/primary/clients.data

    3.  For each client CID, if new_msg.username in
            datastore/$CLUSTER_ID/primary/$CID/following.data
        write new_msg to
            datastore/$CLUSTER_ID/primary/$CID/timeline.data
        with server_flag=1
    
*/

/*
    For this step focus on:

    a. Coordinator tracks CID->ClusterID
    b. SyncService inits by issuing FetchGlobalUsers

    command line args:
        sid/cluster_id
        coord_addr
        self_addr
*/

class SyncService {
    // Member data
    std::string sid; // clusterID
    std::string coord_addr;
    std::string hostname;
    std::string port;
    std::vector<std::string> global_client_table;
    std::queue<UnflaggedDataEntry> entries_to_forward;

    // RPC stuff
    std::string coord_addr;
    std::unique_ptr<SNSCoordinatorService::Stub> coord_stub_;

    // Helpers
    void RegisterWithCoordinator(const Registration& reg);
    void UpdateGlobalClientTable();
    void ForwardMessages();
    void RecvMessageForwards();
public:
    SyncService(const std::string& caddr, const std::string& host, const std::string& p, const std::string& id) 
        : coord_addr(caddr), hostname(host), port(p), sid(id) {
        // Init stub
        coord_stub_ = std::unique_ptr<SNSCoordinatorService::Stub>(
            SNSCoordinatorService::NewStub(
                grpc::CreateChannel(
                    coord_addr, grpc::InsecureChannelCredentials()
                )
            )
        );

        // Issure RPC to register with the coordinator
        // Fill RPC
        Registration reg;
        reg.set_sid(sid);
        reg.set_hostname(host);
        reg.set_port(port);
        reg.set_type("SYNCSERVICE");

        RegisterWithCoordinator(reg);
                
        // Issue RPC to get global clients and write to file (X)
        UpdateGlobalClientTable();
    }
};

void SyncService::RegisterWithCoordinator(const Registration& reg) {

    /*
    We either get
        404:OK -> sleep(3), try again
        200:OK -> Good to go
    */

    Reply repl;
    ClientContext ctx;
    // Dispatch
    Status stat = coord_stub_->RegisterSyncService(&ctx, reg, &repl);
    // Error handling
    if (!stat.ok()) {//(!)
        std::cout << "SyncService reg error for:\nsid=" << sid << '\n';//(!)
        return;
    }
    
    if (repl.msg() == "404") {
        std::cout << "Server cluster not found for\nsid=" << sid << "\ntrying again in 2s\n";//(!)
        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
        return RegisterWithCoordinator(reg);
    }
    std::cout << "SyncService registered\n";//(!)
    
}
void SyncService::UpdateGlobalClientTable() {
    // * Issue RPC to get all CID from coordinator
    GlobalUsers glob;
    ClientContext ctx;

    Request req;
    req.set_username("SYNC");

    Status stat = coord_stub_->FetchGlobalClients(&ctx, req, &glob);

    // * Update global_client_table
    // (!) Optimize by overwriting, then clearing space at the end
    int n_users = glob.cid_size();
    global_client_table.resize(n_users);
    for (int i = 0; i < n_users; ++i) {
        global_client_table[i] = glob.cid(i);
    }
}
void SyncService::ForwardMessages() {

    // * If msgs to fwd, open stream
    ClientContext ctx;
    std::shared_ptr<ClientReaderWriter<UnflaggedDataEntry, UnflaggedDataEntry>> stream(
        coord_stub_->Forward(&ctx)
    );

    // * Forward any messages to coord
    while (!entries_to_forward.empty()) {
        stream->Write(entries_to_forward.pop());
    }
}
void SyncService::RecvMessageForwards() {

}

int main() {
    // parse command line params
    // init SyncService
    // Run multithreaded sniffer call
    return 1;
}

// class SyncService {
//     std::string coord_info;
//     std::string cluster_sid;
//     std::string hostname;
//     std::string port;
//     std::unique_ptr<SNSCoordinatorService::Stub> coord_stub_;
// public:
//     void register_with_coordinator();
//     void update_local_clients();
//     void check_for_outbound();
//     void check_for_inbound();
//     void forward_messages();
//     void write_to_timeline(std::string cid);
// }

//(!)(!)(!)(!) vvv
// #define FILE_DELIM (std::string("|:|"))

// // courtesy of SO
// static std::time_t to_time_t(const std::string& str, bool is_dst = false, const std::string& format = "%Y-%b-%d %H:%M:%S") {
//     std::tm t = {0};
//     t.tm_isdst = is_dst ? 1 : 0;
//     std::istringstream ss(str);
//     ss >> std::get_time(&t, format.c_str());
//     return mktime(&t);
// }

// std::vector<std::string> split_string(std::string s, std::string delim=FILE_DELIM) {
//     std::vector<std::string> parts;
//     size_t pos = 0;
//     std::string token;
//     while ((pos = s.find(delim)) != std::string::npos) {
//         token = s.substr(0, pos);
//         parts.push_back(token);
//         s.erase(0, pos + delim.length());
//     }
//     parts.push_back(s);
//     return parts;
// }

// std::vector<Message> read_timeline_data(std::string fname) {
//     // Parse timeline datastore file into a vector of grpc Messages
//     std::vector<Message> messages;
//     std::string line;
//     std::ifstream in_stream(fname);
//     // Parse input file
//     while(getline(in_stream, line)){
//         Message msg;
//         // Parse line in file
//         std::vector<std::string> parts = split_string(line);
//         // Skip file lines w/o TIME|CID|MSG
//         if (parts.size() != 3) {
//             continue;
//         }
//         // Generate grpc Timestamp
//         std::time_t timestmp = to_time_t(parts[0]);
//         Timestamp* timestamp = new Timestamp();
//         *timestamp = google::protobuf::util::TimeUtil::TimeTToTimestamp(timestmp);
//         // Set Message descriptors and add to vec
//         msg.set_allocated_timestamp(timestamp);
//         msg.set_username(parts[1]);
//         msg.set_msg(parts[2]);
//         messages.push_back(msg);
//     }
//     return messages;
// }

// void check_update();
// void get_sync_service_addr();
// void write_user_timeline(std::string cid);

// // int main() {
// //     std::cout << "Testing message read-ins\n";
// //     std::vector<Message> messages = read_timeline_data("test_input.txt");
// // }
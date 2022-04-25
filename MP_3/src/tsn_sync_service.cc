/* ------- sync service ------- */
#include <unistd.h>
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
#include <unordered_set>

#include "schmokieFS.h"
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
using csce438::Forward;
using csce438::FlaggedDataEntry;
using csce438::SNSCoordinatorService;

#define DEFAULT_HOST        (std::string("0.0.0.0"))

// This is a bit wasteful, but it's nice to have the output help show
// people everthing that's going one, and how the network propogates
#define DEBUG               (0)


struct ClientFollowerEntry {
    std::string cid;
    std::vector<std::string> followers;
};

class SyncService {

    
    // Set how often the sync services propogate data across clusters
    // in milliseconds, taken as command line arg
    int SYNC_FREQ;

    // Member data
    std::string sid; // this SyncService's clusterID
    std::string coord_addr;
    std::string hostname;
    std::string port;

    // All client ids across all server clusters
    std::vector<std::string> global_client_table;
    // Local clients and who they follow
    std::vector<ClientFollowerEntry> client_follower_table;

    // Forwarding containers
    std::queue<FlaggedDataEntry> entries_to_forward;      // come from .../$CID/sent_messages.data
    std::queue<FlaggedDataEntry> entries_recvd;           // go to .../$CID/timeline.data

    // RPC stuff
    std::unique_ptr<SNSCoordinatorService::Stub> coord_stub_;

    // RPC issuers
    void RegisterWithCoordinator(const Registration& reg, int count=0);
    void UpdateGlobalClientTable();
    void ForwardHandler();
    void UpdateAllFollowerData();
    std::vector<std::string> FetchFollowersForUser(const std::string& cid);

    // Helpers
    ClientFollowerEntry* get_client_follower_entry(std::string cid);
    void Spin();
    
public:
    SyncService(const std::string& caddr, const std::string& host, const std::string& p, const std::string& id, int freq);
};
SyncService::SyncService(const std::string& caddr, const std::string& host, const std::string& p, const std::string& id, int freq) 
    : coord_addr(caddr), hostname(host), port(p), sid(id), SYNC_FREQ(freq) {
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
            
    // * Run all our service methods
    Spin();
}
void SyncService::Spin() {
    while (true) {
        if(DEBUG) std::system("clear");
        if(DEBUG) std::cout << "Spin, then sleeping for " << SYNC_FREQ << '\n';

        // * Fetch all followers for each client store in client_follower_table,
        //    then write to $CID/followers.data for each
        UpdateAllFollowerData();

        // * Update global clients
        UpdateGlobalClientTable();

        // * Establish stream w/ coordinator, read sent_messages.data and handle forwards sequentially
        //   writing to .../$CID/timeline.data where applicable
        ForwardHandler();

        // * goodnight, sweet thread!
        std::this_thread::sleep_for(std::chrono::milliseconds(SYNC_FREQ));
    }
}
std::vector<std::string> SyncService::FetchFollowersForUser(const std::string& cid) {
    // * Issue RPC to coordinator retrieving the tracked followers for cid
    Reply repl;
    ClientContext ctx;
    Request req;
    req.set_username(cid);
    Status stat = coord_stub_->FetchFollowers(&ctx, req, &repl);
    if (!stat.ok()) {
        return std::vector<std::string>();
    }

    // * If success, copy all followers to vector and return
    std::vector<std::string> followers;
    for (int i = 0; i < repl.following_users_size(); ++i) {
        followers.push_back(repl.following_users(i));
    }
    return followers;
}
void SyncService::UpdateAllFollowerData() {
    /*
        Update the in-memory client_follower_table
        Update the on-disc $CID/followers.data
    */

    // * Read all client cids from fs
    std::vector<std::string> clients = schmokieFS::SyncService::read_local_cids_from_fs(sid, "primary");

    // * For each client in fs, fetch their followers from the coordinator
    for (const std::string cid_: clients) {
        // * Get their ClientFollowerEntry or create if new
        ClientFollowerEntry* cf_entry = get_client_follower_entry(cid_);
        if (cf_entry == nullptr) { // make new
            ClientFollowerEntry cfe_new;
            cfe_new.cid = cid_;
            client_follower_table.push_back(cfe_new);
            cf_entry = get_client_follower_entry(cid_);
        }
        // * Update the in-memory client_follower_table
        cf_entry->followers = FetchFollowersForUser(cid_);
    }
    
    // * Update ALL followers on disc, we're aiming to do this in batches so we can
    //   acquire and release a lock quicker than if we do it one user at a time
    for (const ClientFollowerEntry& cli_fe: client_follower_table) {
        schmokieFS::SyncService::update_followers(sid, cli_fe.cid, cli_fe.followers, "primary");
    }
}
void SyncService::RegisterWithCoordinator(const Registration& reg, int count) {

    /*
    A server cluster with the given SID must be registered with coordinator
    when we issue this RPC.

    So we either get
        404:OK -> sleep(2), then try again
        200:OK -> Good to go
    */
    // If we get 5 consecutive rejections, stop trying
    if (count == 5) {
        std::cout << "Sync service reg, max tries reached, exiting\n";
        exit(0);
    }

    Reply repl;
    ClientContext ctx;
    // Dispatch
    Status stat = coord_stub_->RegisterSyncService(&ctx, reg, &repl);
    // Error handling
    if (!stat.ok()) {
        return;
    }
    
    if (repl.msg() == "404") {
        if (DEBUG) std::cout << "Server cluster not found for\nsid=" << sid << "\ntrying again in 2s\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
        return RegisterWithCoordinator(reg, count+1);
    }
    
    if (DEBUG) std::cout << "SyncService registered\n";
}
void SyncService::UpdateGlobalClientTable() {
    // * Issue RPC to get all CID from coordinator
    GlobalUsers glob;
    ClientContext ctx;

    Request req;
    req.set_username("SYNC");

    Status stat = coord_stub_->FetchGlobalClients(&ctx, req, &glob);

    // * Update global_client_table
    int n_users = glob.cid_size();
    global_client_table.resize(n_users);
    for (int i = 0; i < n_users; ++i) {
        global_client_table[i] = glob.cid(i);
    }

    // * Write to .../$CLUSTER_ID/$SERVER_TYPE/global_clients.data
    schmokieFS::SyncService::write_global_clients(sid, global_client_table, "primary");
    if (DEBUG) std::cout << "Updated global clients in memory and on cluster disc\n";
}
void SyncService::ForwardHandler() {

    if (DEBUG) std::cout << "Checking for bidi-forwards\n";

    /*
        The primary downside to this implementation (which simplifies our lock service)
        is that it doesn't scale well to many threads. Since we treat local messages the
        same as global messages, mulithreading our stream->Read/stream->Write runs the risk
        that some local messages will not be sent until the next cycle.

        After this, without a counter (lamport clock, etc.), we cannot guarentee messages
        across server clusters are ordered properly.
    */
    // * Open stream and send stream init message
    ClientContext ctx;
    std::shared_ptr<grpc::ClientReaderWriter<Forward, Forward>> stream {
        coord_stub_->ForwardEntryStream(&ctx)
    };
    Forward stream_init_msg;
    stream_init_msg.add_cid("SYNCINIT");
    stream_init_msg.set_entry(sid);
    stream->Write(stream_init_msg);

    // --- Handle outbound forwards ---
    // * Read in and delete .../$SID/sent_messages.tmp if it exists
    std::vector<FlaggedDataEntry> sent_messages = schmokieFS::SyncService::gather_sent_msgs(sid, "primary");

    if (DEBUG) {
        std::cout << "--- sent_messages.tmp ---\n";
        for (const auto& m: sent_messages) {
            std::cout << m.entry() << '\n';
        }
        std::cout << "      ---     \n";
    }

    // * For each message in sent_messages
    for (const FlaggedDataEntry& fd_entry: sent_messages) {
        // * Extract sender
        std::string sender_cid = fd_entry.cid();

        // * Gather the followers for the sender
        std::vector<std::string> sender_followers = get_client_follower_entry(sender_cid)->followers;

        // * Composer Forward{ followers of sender | sent_message }
        Forward outbound_fwd;
        for(const std::string& follower_cid_: sender_followers) {
            outbound_fwd.add_cid(follower_cid_);
        }
        outbound_fwd.set_entry(fd_entry.entry());

        if (DEBUG) std::cout << "Forwarding message: " << outbound_fwd.entry() << '\n';        

        // * Send this to coordinator
        stream->Write(outbound_fwd);
    }
    
    // * Wait for writes to finish and signal to coordinator it can start forwarding msgs
    stream->WritesDone();
    
    if (DEBUG) std::cout << "Done with outbounds\n\n";

    // --- Handle inbound forwards ---
    // * For each read Forward{ receiver cid | sent_message }
    Forward inbound_fwd;
    while(stream->Read(&inbound_fwd)) {
        // * Unpack forward
        std::string recvr_cid = inbound_fwd.cid(0);
        std::string entry = inbound_fwd.entry();

        if (DEBUG) {
            std::cout << "Got inboudn for cid=" << recvr_cid << '\n';
            std::cout << entry << "\n\n";
        }

        // * Write sent_message to .../$receiver_cid/timeline.data with IOflag=1
        schmokieFS::SyncService::write_fwd_to_timeline(sid, recvr_cid, entry, "primary");
    }

    Status stat = stream->Finish();
    if (!stat.ok()) {
        if (DEBUG) std::cout << "FWDSTREAM DESTRUCTOR ERR\n";
    }
}
ClientFollowerEntry* SyncService::get_client_follower_entry(std::string cid) {
    for (int i = 0; i < client_follower_table.size(); ++i) {
        if (client_follower_table[i].cid == cid) {
            return &client_follower_table[i];
        }
    }
    return nullptr;
}

int main(int argc, char** argv) {
    
    std::string helper = "Calling convention for sync_service:\n\n"
                         "./tsn_sync_service -c <coordIP>:<coordPort> -s <serverID> -p <port> -q <refreshFrequencyMilli>\n\n";

    if (argc == 1) {
        std::cout << helper;
        return 0;
    }

    std::string port = "3011";
    std::string coord;
    std::string serverID;
    int sync_freq = 10000;
    // parse command line params
    int opt = 0;
    while ((opt = getopt(argc, argv, "c:s:p:q:")) != -1) {
        switch (opt) {
            case 'c':
                coord = optarg;
                break;
            case 's':
                serverID = optarg;
                break;
            case 'p':
                port = optarg;
                break;
            case 'q':
                sync_freq = std::stoi(optarg);
                break;
            default:
                std::cerr << "Invalid CL arg\n";
                std::cerr << helper;
                return 0;
        }
    }
    
    // Start sync service which spins inside the constructor
    SyncService synchro(coord, DEFAULT_HOST, port, serverID, sync_freq);
    return 0;
}

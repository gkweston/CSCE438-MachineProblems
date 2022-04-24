/* ------- sync service ------- */
/* (!)
    Start this by reading in all messages for ${UID}.txt
    keep { TIME | UID | MSG } in memory

    we'll keep this in memory until it's sent on the wire

    use stat(), or similar, to check when to read in mem

    keep TIME last_data_diff on hand

    VERY sloppy, hacky, needs polishing all over
*/

// (!) New flag (X)=Mutual Exclusion may be necessary

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

#include "tsn_database.h"
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

#define DEFAULT_HOST    (std::string("0.0.0.0"))
#define SLEEP_MS        (7000)

struct ClientFollowerEntry {
    std::string cid;
    std::vector<std::string> followers;
    // std::vector<std::string> following;
};

class SyncService {
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
    // void check_new_local_msgs(); // ---(!) DEPRECATED
    // void proc_entry_recvs();     // ---(!) DEPRECATED
    // void update_client_follower_table();     // ---(!) DEPRECATED
    // FlaggedDataEntry compose_stream_init_msg(); // ---(!) DEPRECATED
    // std::unordered_set<std::string> get_clients_followed(); // ---(!) DEPRECATED
    ClientFollowerEntry* get_client_follower_entry(std::string cid);
    // std::vector<std::string> get_clients_who_follow(std::string cid); // ---(!) DEPRECATED
    void Spin();
    
public:
    SyncService(const std::string& caddr, const std::string& host, const std::string& p, const std::string& id);
};
SyncService::SyncService(const std::string& caddr, const std::string& host, const std::string& p, const std::string& id) 
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
            
    // * Run all our service methods
    Spin();
}
void SyncService::Spin() {
    while (true) {
        std::cout << "Issuing spin cycle! then sleeping for " << SLEEP_MS << '\n';//(!)

        // * Fetch all followers for each client store in client_follower_table,
        //    then write to $CID/followers.data for each
        UpdateAllFollowerData();

        // * Update global clients
        UpdateGlobalClientTable();

        // * Establish stream w/ coordinator, read sent_messages.data and handle forwards sequentially
        //   writing to .../$CID/timeline.data where applicable
        ForwardHandler();

        // * goodnight, sweet thread!
        std::this_thread::sleep_for(std::chrono::milliseconds(SLEEP_MS));
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
        std::cout << "ERR ON FETCHFOLLOWERS for cid=" << cid << '\n';//(!)
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
    std::vector<std::string> clients = SchmokieFS::SyncService::read_local_cids_from_fs(sid, "primary");

    // * For each client in fs, fetch their followers from the coordinator
    for (const std::string cid_: clients) {
        // * Get their ClientFollowerEntry or create if new
        ClientFollowerEntry* cf_entry = get_client_follower_entry(cid_);
        if (cf_entry == nullptr) { // make new
            ClientFollowerEntry cfe_new;
            cfe_new.cid = cid_;
            client_follower_table.push_back(cfe_new);

            /* (!) EASY FIX (!) */
            // cf_entry = &cfe_new; //(!) don't know if pushing to vec will change ref (!)(!)
            // (!) wasteful but we know it will resolve correctly
            cf_entry = get_client_follower_entry(cid_);
        }
        // * Update the in-memory client_follower_table
        cf_entry->followers = FetchFollowersForUser(cid_);
    }
    
    // * Update ALL followers on disc, we're aiming to do this in batches so we can
    //   acquire and release a lock quicker than if we do it one user at a time
    for (const ClientFollowerEntry& cli_fe: client_follower_table) {
        SchmokieFS::SyncService::update_followers(sid, cli_fe.cid, cli_fe.followers, "primary");
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
    if (!stat.ok()) {//(!)
        std::cout << "SyncService reg not found for sid=" << sid << '\n';//(!)
        return;
    }
    
    if (repl.msg() == "404") {
        std::cout << "Server cluster not found for\nsid=" << sid << "\ntrying again in 2s\n";//(!)
        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
        return RegisterWithCoordinator(reg, count+1);
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

    //(!) debug view
    // |
    // std::cout << "global clients:\n";//(!)
    // for (int i = 0; i < global_client_table.size(); ++i) {
    //     std::cout << global_client_table[i] << '\n';//(!)
    // }
    // +---(!)

    // * Write to .../$CLUSTER_ID/$SERVER_TYPE/global_clients.data
    SchmokieFS::SyncService::write_global_clients(sid, global_client_table, "primary");
    std::cout << "Updated global clients in memory and on cluster disc\n";//(!)
}
// void SyncService::check_new_local_msgs() { // ---(!) DEPRECATED?
//     /*
//         Check for outbound client messages in .../$CID/sent_messages.tmp and read them into
//         memory. Remove this file so we don't double send. These messages are buffered
//         next time we do a forward exchange with the coordinator.
//     */
//     //(!) debuggovision
//     // |
//     // std::cout << "size entries_to_forward before:" << entries_to_forward.size() << "\n";//(!)
//     // +---(!)
    

//     // * Update local client table
//     update_client_follower_table();

//     // * For each local client, check if there are new outbound message(s)
//     for (const ClientFollowerEntry& client_: client_follower_table) {
//         // std::vector<FlaggedDataEntry> new_entries = SchmokieFS::SyncService::check_update_by_cid(sid,client_.cid, "primary");
//         std::vector<FlaggedDataEntry> new_entries = SchmokieFS::SyncService::check_sent_by_cid(sid, client_.cid, "primary");
//         // * Buffer each message to be forwarded later
//         for (int i = 0; i < new_entries.size(); ++i) {
//             entries_to_forward.push(new_entries[i]);
//         }
//     }

//     //(!) debuggovision
//     // |
//     // std::cout << "size entries_to_forward before:" << entries_to_forward.size() << "\n";//(!)
//     // std::cout << "entries:\n";
//     // while (!entries_to_forward.empty()) {
//     //     std::cout << entries_to_forward.front().entry() << '\n';
//     //     entries_to_forward.pop();
//     // }
//     // +---(!)

//     std::cout << "Pulled local messages into entries_to_forward queue\n";//(!)
// }
// std::unordered_set<std::string> SyncService::get_clients_followed() {       // ---(!) DEPRECATED?
//     // For each local client add those they follow to clients_followed,
//     // at this point in execution, the client_follower_table should be up to date

//     // * Clear the set
//     std::unordered_set<std::string> clients_followed;

//     // * Iterate over all local clients
//     for (const ClientFollowerEntry& e: client_follower_table) {
//         // * Iterate all clients they follow and add to set iff not already there
//         for (const std::string& f: e.following) {
//             if (clients_followed.find(f) == clients_followed.end()) {
//                 clients_followed.insert(f);
//             }
//         }
//     }
//     // * return set to caller
//     return clients_followed;
// }
/*RE(!)
    Outbound:

    Read in .../$SID/primary/sent_messages.data

    for each entry in sent_messages.data        --> Order here matters!
        Read in entry and extract sender.cid
        Read in followers of sender.cid
            ForwardIn{ followers_of_user | sent_message }
            Send forward to coordinator
    
    Inbound:

    Read in Forward{ followers_of_user | sent_message}
    for each followers_of_user
        write sent_message to .../$follower_cid/timeline.data where IOflag=1

    
    -> Some of these incoming will have been local which we bounced back
    -> Only respects SOME message order

*/
// RE(!) Just keep the stream open?
//       Maybe just issue a thread that keeps calling this method?
/*
DBG:
    We're double sending or more likely we are double reading
    msg doesn't double until we recv it from server

*/
void SyncService::ForwardHandler() {

    std::cout << "Checking for bidi-forwards\n";//(!)

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
    
    // RE(!)? could thread here, but given that we gather all forwards each time we call this method
    //        and close the stream after reading it doesn't necessarily make sense to spawn a blocking
    //        thread

    // * Read in and delete .../$SID/sent_messages.tmp if it exists
    std::vector<FlaggedDataEntry> sent_messages = SchmokieFS::SyncService::gather_sent_msgs(sid, "primary");

    //(!)-------------------------------------------------(!)
    std::cout << "--- sent_messages.tmp ---\n";
    for (const auto& m: sent_messages) {
        std::cout << m.entry() << '\n';
    }
    std::cout << "      ---     \n";
    //(!)-------------------------------------------------(!)

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

        //(!)-------------------------------------------------(!)
        std::cout << "Forwarding message: " << outbound_fwd.entry() << '\n';
        //(!)-------------------------------------------------(!)

        // * Send this to coordinator
        stream->Write(outbound_fwd);
    }
    
    // * Wait for writes to finish and signal to coordinator it can start forwarding msgs
    stream->WritesDone();

    //(!)-------------------------------------------------(!)
    std::cout << "Done with outbounds\n\n";
    //(!)-------------------------------------------------(!)

    // --- Handle inbound forwards ---
    
    // RE(!)? similarly could be threaded, but the coordinator either sends forwards for clients
    //        we server, or it sends us nothing, then we close the stream. We're not really waiting
    //        around for anything, and we don't keep this stream open.

    // * For each read Forward{ receiver cid | sent_message }
    Forward inbound_fwd;
    while(stream->Read(&inbound_fwd)) {
        // * Unpack forward
        std::string recvr_cid = inbound_fwd.cid(0);
        std::string entry = inbound_fwd.entry();

        //(!)-------------------------------------------------(!)
        std::cout << "Got inboudn for cid=" << recvr_cid << '\n';
        std::cout << entry << "\n\n";
        //(!)-------------------------------------------------(!)

        // * Write sent_message to .../$receiver_cid/timeline.data with IOflag=1
        SchmokieFS::SyncService::write_fwd_to_timeline(sid, recvr_cid, entry, "primary");
    }

    Status stat = stream->Finish();
    if (!stat.ok()) {
        std::cout << "FWDSTREAM DESTRUCTOR ERR\n";//(!)
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
// void SyncService::update_client_follower_table() {
//     // Since ClientFollowerEntry contains a vector, this method is possibly slower
//     // or as-slow-as just clearing the table and generating from scratch

//     // * Get all CIDs in this cluster
//     std::vector<std::string> local_clients = SchmokieFS::SyncService::read_local_cids_from_fs(sid, "primary");

//     // * For each CID in this cluster, read in their followers
//     for (const std::string& client_: local_clients) {
//         std::vector<std::string> followers_ = SchmokieFS::SyncService::read_followers_by_cid(sid, client_, "primary");

//         // * Check if client following entry in table - if not, add to table
//         ClientFollowerEntry* cfentry = get_client_follower_entry(client_);
//         if (cfentry != nullptr) { // exists
//             cfentry->followers = followers_;        //(!) faster better way to copy vector?
//         } else { // DNE, add to client_follower_table
//             ClientFollowerEntry cfentry_new;
//             cfentry_new.cid = client_;
//             cfentry_new.followers = followers_;
//             client_follower_table.push_back(cfentry_new);
//         }
//     }

//     //(!) debug view
//     // |
//     // std::cout << "local clients, should be:\n";//(!)
//     // std::cout << "1->111 112\n2->221 222\n3->331 332\n---\n";//(!)
//     // for (const auto& c: client_follower_table) {
//     //     std::cout << c.cid << "->";
//     //     for (const auto& f : c.following) {
//     //         std::cout << f << ' ';
//     //     }
//     //     std::cout << '\n';
//     // }
//     // std::cout << '\n';
//     // +---(!)

//     std::cout << "Update local client table in memory from cluster disc\n";
// }
// std::vector<std::string> SyncService::get_clients_who_follow(std::string cid) { // ---(!) DEPRECATED?
//     // Return all local clients who have cid in there following vector on the client_follower_table
//     std::vector<std::string> followers_of;
//     // * Iterate over clients in client_follower_table
//     for (const ClientFollowerEntry& cfentry: client_follower_table) {
//         // * Check if the given cid exists in this client's following vector
//         for (const std::string& usr_: cfentry.following) {
//             // * If so, add this client to followers_of
//             if (cid == usr_) {
//                 followers_of.push_back(cfentry.cid);
//                 break;
//             }
//         }
//     }
//     return followers_of;
// }
// void SyncService::proc_entry_recvs() { // Database IO method // ---(!) DEPRECATED

//     //(!) debugg-o-vision   (!)     (!)     (!)
//     // |
//     // // manual entries_recvd filling so we can test this function
//     // FlaggedDataEntry e11;
//     // e11.set_cid("111");
//     // e11.set_entry("0|:|2022-04-16T20:28:03Z|:|111|:|hello, user1!, how's it going?");
//     // FlaggedDataEntry e12;
//     // e12.set_cid("112");
//     // e12.set_entry("0|:|2022-04-16T20:28:03Z|:|112|:|hello, user1 from 112!");
//     // FlaggedDataEntry e21;
//     // e21.set_cid("222");
//     // e21.set_entry("0|:|2022-04-16T20:28:07Z|:|222|:|user 2, I'm 222");
//     // FlaggedDataEntry e22;
//     // e22.set_cid("221");
//     // e22.set_entry("0|:|2022-04-16T20:28:07Z|:|221|:|Hi user 2, I've been trying to get a hold of you about your car's extended warranty!");
//     // entries_recvd.push(e11);
//     // entries_recvd.push(e12);
//     // entries_recvd.push(e21);
//     // entries_recvd.push(e22);
//     // +---(!)    (!)     (!)     (!)     (!)

//     // At this point, we've sent all our forwards to coordinator, and we may have
//     // received forwards that need to be processed

//     // * Write any forwards received to the the relevant users' timeline
//     while (!entries_recvd.empty()) {
//         // (!) we could save our pop for the end if there's some IO error
//         FlaggedDataEntry ufdentry = entries_recvd.front();
//         entries_recvd.pop();

//         std::string sender = ufdentry.cid();
//         std::vector<std::string> clients_who_follow_sender = get_clients_who_follow(sender);
        

//         // * If the coordinator forwarded us a msg for which we don't have a receiver, there must
//         //   have been a client to register w/ coordinator since the last time we updates local_clients_table
//         //   do that now, and if still none - just throw msg away (for now)
//         if (clients_who_follow_sender.size() == 0) {
//             std::cout << "No local clients found who follow " << sender << " updating local clients...\n";//(!)
//             // update_client_follower_table(); //(!) do this before forwards, not after!
//             clients_who_follow_sender = get_clients_who_follow(sender);

//             if (clients_who_follow_sender.size() == 0) {//(!)
//                 // If still none, there's probably a bug, just throw msg away for now (!)
//                 std::cout << "Still no clients who follow " << sender << " something must be wrong...\n";//(!)
//                 continue;
//             }
//         }

//         //(!) debugg-o-vision   (!)     (!)     (!)
//         // |
//         // std::cout << "sender=" << sender << "\nlocal clients who follow:\n";
//         // for (const auto& s: clients_who_follow_sender) {
//         //     std::cout << s << '\n';
//         // }
//         // +---(!)
    

//         // * For each client who follows this sender, write to their .../$CID/timeline.data
//         for (const std::string& client_: clients_who_follow_sender) {
//             //(!) debugg-o-vision   (!)     (!)     (!)
//             // |
//             // std::cout << "writing to timeline for cid=" << client_ << "\nentry=" << ufdentry.entry() << '\n';
//             // +---(!)
//             SchmokieFS::SyncService::write_fwd_to_timeline(sid, client_, ufdentry.entry(), "primary");
//             std::cout << "Wrote message receipts to timeline with server_flag=1\n"; //(!)
//         }
//     }
//     std::cout << "End proc_entry_recvs\n";//(!)
// }

int main(int argc, char** argv) {

    /*
    Simplified args:
        -c <coord_hostname>:<coord_port>
        -s <sid/cluster_id>
        -p <port>
    */
   if (argc == 1) {
       std::cout << "Calling convention for sync_service:\n\n";
       std::cout << "./tsn_sync_service -c <coordIP>:<coordPort> -s <serverID> -p <port>\n\n";
       return 0;
   }

    std::string port = "3011";
    std::string coord;
    std::string serverID;
    // parse command line params
    int opt = 0;
    while ((opt = getopt(argc, argv, "c:s:p:")) != -1) {
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
            default:
                std::cerr << "Invalid CL arg\n";
        }
    }
    
    // Start sync service which spins inside the constructor
    SyncService synchro(coord, DEFAULT_HOST, port, serverID);
    return 0;
}

// (!)
// void StartSyncService(std::string caddr_, std::string sid, std::string p) {
//     SyncService synchro(caddr_, DEFAULT_HOST, p, sid);

//     /*
//     Test efforts:
//         set .../[1,2]/sent_messages.data flag=1
//         have the coordinator write all of these forwards on receipt
//         have the coordinator send hardcoded forwards
//     */
//     // synchro.test("Testing ForwardHandler\n");
//     // std::cout << "-(T)-\nexiting.\n";
//     // exit(0);
//     //    -------(T)
// }//(!)
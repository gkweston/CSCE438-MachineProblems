/*

"Pseudoclass" Schmokie File System - this represents a real-world distributed
file system, which would run a server to server distributed read/writes from
non-local clients.

The format of this (probably) breaks some header file management best-practices,
but should not break the project. This may be refactored on delivery, or it may
just be kept this way (because it works for our local system!)

tsn_database is a pseudoclass representing the Schmokie FileSystem, which manages
our tsn data on disc. It includes 3 namespaces and a few helpers, all wrapped in
the namespace schmokieFS for easy access and seperation.

namespace schmokieFS::SyncService
    Manage the database files for the follower sync service, some of these files
    are shared with PrimaryServer, where they are marked below as
        Read:           S:[R]
        Write:          S:[W]
        Exclusive:      S:<X>       -> where a distributed lock service is required


namespace schmokieFS::PrimaryServer
    Manage database files for the initial primary server which is prone to
    faults, files shared with SyncService, are marked below as
        Read:           P:[R]
        Write:          P:[W]
        Exclusive:      P:<X>       -> where a distributed lock service is required        

namespace schmokieFS::SecondaryServer
    Manage database files for the backup server which is brought online, reads
    ALL datastore/* files and primarily just calls stat() before copying the
    new version to it's file system directory

    This is really any backup server which will run these methods. If Primary goes
    offline, we simply promote a backup to Primary, which writes its all files
    to the primary FS and assumes primary duties

    This setup allows N Secondary servers which can act as failsafe, including an
    old primary that comes back online.

schmokieFS Schema:

$FS_CWD/
    datastore/
        $(CLUSTER_ID}/
            ${SERVER_TYPE}/
                global_clients.data         P:[R], S:[W]
                sent_messages.tmp           P:[W]<X>, S:[R]<X>
                local_clients/
                    ${CID}/
                        timeline.data       P:[RW]<X>, S:[W]<X>
                        following.data      P:[WR], S:[R]
                        followers.data      P:[WR]<X>, S:[WR]<X>

Each P:[R] technically writes a file, as it needs to flip the 1st io_flag byte
for each line, so it doesn't read this line again.

IOflag is only used for PrimaryServer, and indicates a write has happened by
another proccess which should be propogated by the server. After propogation,
flip this leading byte to 0. SyncService now checks if fwds exists by checking
if .../$CID/sent_messages.tmp exists, deleting after fwd. SecondaryServer simply
calls stat() to copy diff'd files.

In some cases, we traverse this filesystem to collect metadata. 
    e.g., to read all local clients on the server cluster, SyncService
    reads all filenames in datastore/$CLUSTER_ID/$SERVER_TYPE/local_clients/

NOTE:   We don't worry about file cleanup if a client disconnects. This could simply be
        its own small cleanup-service. We also don't worry about unfollows at this stage.

        This should be it's own linked library at this point,
        but it's easier to just #include it (for now...)
*/

/*
 (!) Easy updates:
    pass params as "const T&" where possible
    use the newer check_mkdir(...)
*/

#include <ctime>
#include <cstdio>
#include <sys/stat.h>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <iomanip>
#include <sstream>
#include <grpc++/grpc++.h>
#include <google/protobuf/util/time_util.h>

// ---> NOTE (!)
#include <experimental/filesystem>
// requires g++ <files>.cc -lstdc++fs

#include "sns.grpc.pb.h"

using google::protobuf::Timestamp;
using csce438::Message;
using csce438::FlaggedDataEntry;

#define FILE_DELIM (std::string("|:|"))

namespace schmokieFS {

// For file reads
std::string FS_CWD = std::experimental::filesystem::current_path().string();

bool file_exists(const std::string& path_) {
    // Return true iff file exists at path_
    struct stat buf;
    return (stat (path_.c_str(), &buf) == 0);
}
static std::time_t to_time_t(const std::string& str, bool is_dst = false, const std::string& format = "%Y-%b-%d %H:%M:%S") {
    // Cast a timestamp string to time_t, useful for gRPC timestamp conversions
    std::tm t = {0};
    t.tm_isdst = is_dst ? 1 : 0;
    std::istringstream ss(str);
    ss >> std::get_time(&t, format.c_str());
    return mktime(&t);
}
std::vector<std::string> split_string(std::string s, std::string delim=FILE_DELIM) {
    // Split a string on delim
    std::vector<std::string> parts;
    size_t pos = 0;
    std::string token;
    while ((pos = s.find(delim)) != std::string::npos) {
        token = s.substr(0, pos);
        parts.push_back(token);
        s.erase(0, pos + delim.length());
    }
    parts.push_back(s);
    return parts;
}
std::string get_last_token(std::string s, std::string delim="/") {
    // Get last token of a string split, we use it to get the metadata in our
    // filesystem naming conventions (e.g. /path/to/CID, extract CID)
	size_t pos = 0;
	while ((pos = s.find(delim)) != std::string::npos) {
		s.erase(0, pos + delim.length());
	}
	return s;
}
std::vector<std::string> get_file_diffs(std::string path_no_ext) { // used only by PrimaryServer now
    
    /*
    Read in user data, anywhere we see flag=1 we'll add this
    line to a vector and return the vector. Overwrite this to sync_flag=0
    */

    std::string line;
    std::vector<std::string> diffd_entries;

    // * Read in the file
    std::ifstream data_stream(path_no_ext + std::string(".data"));
    std::ofstream tmp_stream(path_no_ext + std::string(".tmp"));
    
    while( getline(data_stream, line) ) {
        // * if flag=1, set flag=0, add this line to a vector
        if (line[0] == '1') {
            line[0] = '0';
            diffd_entries.push_back(line);
        }
        // * Write entry to a tempfile
        tmp_stream << line << '\n';
    }
    data_stream.close();
    tmp_stream.close();
    
    // * at end of file rename tempfile as file
    std::string tfile = path_no_ext + ".tmp";
    std::string dfile = path_no_ext + ".data";
    rename(tfile.c_str(), dfile.c_str());

    // * return vector of diff lines
    return diffd_entries;
}
Message entry_str_to_grpc_msg(std::string entry_str) {
    // parts = flag1|time|cid|msg
    std::vector<std::string> parts = split_string(entry_str);

    Message msg;
    if (parts.size() != 4) {
        std::cerr << "entry to gRPC ERR\n";
        msg.set_msg("ERROR");
        return msg;
    }

    std::time_t ttime = to_time_t(parts[1]);
    Timestamp* timestamp = new Timestamp();
    *timestamp = google::protobuf::util::TimeUtil::TimeTToTimestamp(ttime);
    msg.set_allocated_timestamp(timestamp);
    msg.set_username(parts[2]);
    msg.set_msg(parts[3]);

    return msg;
}
std::string grpc_msg_to_entry_str(const Message& msg_, std::string flag="1") {
    // returns an entry formatted as FlaggedDataEntry, no newline
    // 1|time|cid|msg
    std::string time_ = google::protobuf::util::TimeUtil::ToString(msg_.timestamp());
    std::string cid_ = msg_.username();
    std::string umsg_ = msg_.msg();
    return std::string(flag + FILE_DELIM + time_ + FILE_DELIM + cid_ + FILE_DELIM + umsg_);
}
bool check_mkdir(const std::string& path_) {
    // * Check if file exists, if so, do nothing, else create it, print err
    //   return false if err occurs
    if ( !file_exists(path_) ) {
        if ( !std::experimental::filesystem::create_directory(path_) ) {
            std::cerr << "Error on schmokieFS::check_mkdir: " << path_ << '\n';
            return false;
        }	
	}
    return true;
}

namespace SyncService {

    /*
        Target functionallity
            - Read all clients on this cluster into memory
                @datastore/$SID/$SERVER_TYPE/local_clients
            - For a given CID read all following into memory
                @datastore/$SID/$SERVER_TYPE/local_clients/$CID/following.txt
            - If sent_messages.tmp exists, read and return, delete sent_messages.tmp
                @datastore/$SID/$SERVER_TYPE/local_clients/$CID/sent_messages.data
            - Write globally received forwards to
                @datastore/$SID/$SERVER_TYPE/local_clients/$CID/timeline.data
            - Write received global users to
                @datastore/$SID/$SERVER_TYPE/global_clients.data
            - Write all followers to
                @datastore/$SID/$SERVER_TYPE/local_clients/$CID/followers.data
    */

    std::vector<std::string> read_local_cids_from_fs(std::string cluster_sid, std::string stype="primary") {

        std::string path_ = FS_CWD + "/datastore/" + cluster_sid + "/" + stype + "/local_clients";
        std::vector<std::string> local_cids;
        for (const auto& dir : std::experimental::filesystem::directory_iterator(path_)) {
            //* Split and get only the last token which is CID
            std::string cid_token = get_last_token(dir.path(), "/");
            local_cids.push_back(cid_token);
        }
        return local_cids;
    }
    std::vector<std::string> read_followers_by_cid(std::string cluster_sid, std::string cid, std::string stype="primary") {
        // * Generate path string 
        std::string path_ = FS_CWD + "/datastore/" + cluster_sid + "/" + stype + "/local_clients/" + cid + "/followers.data";

        // * Read all entries into vector
        std::ifstream ffollowing(path_);
        std::string usr;

        std::vector<std::string> following;
        while(getline(ffollowing, usr)) {
            if (usr.size() > 0 && usr[0] != '\n') {
                following.push_back(usr);
            }
        }

        // * Pass it back to caller
        return following;
    }
    std::vector<FlaggedDataEntry> gather_sent_msgs(std::string cluster_sid, std::string stype="primary") {
        // Return a vector of FlaggedDataEntry iff there are messages to forward, and no errors occur
        // else return an empty vector
        
        // std::string fpath = FS_CWD + "/datastore/" + cluster_sid + "/" + stype + "/local_clients/" + cid + "/sent_messages.tmp";
        std::string fpath = FS_CWD + "/datastore/" + cluster_sid + "/" + stype + "/sent_messages.tmp";

        // * If no $CID/sent_messages.tmp, then no new msgs to forward, return empty vec
        if (!file_exists(fpath)) {
            return std::vector<FlaggedDataEntry>();
        }

        // <X> lock acquire
        // * If $SID/sent_messages.tmp exists, there are new messages to forward
        //   Read file contents into mem as FlaggedDataEntry
        std::string line;
        std::ifstream data_stream(fpath);

        // Read contents in, DO NOT flip IOflag, into fdata. We're trying to do this quickly so
        // we can lock release as soon as possible, so do data ops after reading into mem
        std::vector<std::string> fdata;
        while( getline(data_stream, line) ) {
            if (line.size() > 0) {  // don't push back any empty lines
                fdata.push_back(line);
            }
        }

        // * Delete file
        std::remove(fpath.c_str());
        // <X> lock release

        // * Process into FlaggedDataEntry
        std::vector<FlaggedDataEntry> entries;
        for(const std::string& s: fdata) {
            // * Extract CID from entry
            std::string sender_cid = split_string(s, FILE_DELIM)[2];

            // * Compose
            FlaggedDataEntry e;
            e.set_cid(sender_cid);
            e.set_entry(s);
            entries.push_back(e);
        }

        // * Return vec of these entries
        return entries;
    }
    void write_fwd_to_timeline(std::string cluster_sid, std::string cid, std::string fwd, std::string stype="primary") {
        // * Generate path string corresponding to this user
        std::string path_ = FS_CWD + "/datastore/" + cluster_sid + "/" + stype + "/local_clients/" + cid + "/timeline.data";

        // * Fwds come as FlaggedDataEntry.entry(), set flag=1
        fwd[0]='1';

        // * Write this to user's timeline file
        std::ofstream user_timeline(path_, std::ios::app);
        user_timeline << fwd << '\n';

    }
    void write_global_clients(std::string cluster_sid, const std::vector<std::string>& global_clients, std::string stype="primary") {
        // * Generate path string
        std::string path_no_ext = FS_CWD + "/datastore/" + cluster_sid + "/" + stype + "/global_clients";
        std::string tfile = path_no_ext + ".tmp";
        // * Write to temp file
        std::ofstream fglob_client_tmp(tfile);
        for (const std::string& cid: global_clients) {
            fglob_client_tmp << cid << '\n';
        }
        // * Rename temp file
        std::string dfile = path_no_ext + ".data";
        rename(tfile.c_str(), dfile.c_str());
    }
    void update_followers(const std::string& cluster_sid, const std::string& cid, const std::vector<std::string>& followers, std::string stype="primary") {
        // * gen path string
        std::string path_no_ext_ = FS_CWD + "/datastore/" + cluster_sid + "/" + stype + "/local_clients/" + cid + "/followers";
        // * write to temp file
        std::string tfile = path_no_ext_ + ".tmp";
        std::ofstream data_stream(tfile);
        for (const std::string& u: followers) {
            data_stream << u << '\n';
        }
        // * rename temp file, placing or overwriting
        std::string dfile = path_no_ext_ + ".data";
        rename(tfile.c_str(), dfile.c_str());
    }

}   // end namespace SyncService

namespace PrimaryServer {
    /*
        Target functionallity
            - Init filesystem for server cluster without disturbing any
              secondary server files that may or may not exist
                @datastore/$SID/primary
            - Init client file when a new client connects, without distrubing
              any files that may or may not exist for that client
                @datastore/$SID/primary/local_clients/$CID
            - Write outbound messages from a client with IOflag=1
                @datastore/$SID/primary/local_clients/$CID/sent_messages.tmp
            - Update user following.data when a valid FOLLOW command is issued for a
              local OR global user
                @datastore/$SID/primary/local_clients/$CID/following.data
            - Read in global clients to serve LIST cmd and check FOLLOW cmds
                @datastore/$SID/primary/global_clients.data
            - Read messages from user timeline where IOflag=1, set IOflag=0, to
              be served to the user in TIMELINE mode
                @datastore/$SID/primary/local_clients/$CID/timeline.data
            - Write messages to user timeline where IOflag=0 iff message originated
              on local cluster
                @datastore/$SID/primary/local_clients/$CID/timeline.data
    */
    std::vector<Message> check_timeline_updates(const std::string& sid, const std::string& cid) {
        /*
            Get new messages on users timeline at datastore/$SID/primary/local_clients/$CID/timeline.data
            which will then be served to the user.
        */
        std::vector<Message> new_messages;

        // * Get file diffs
        std::string path_no_ext = FS_CWD + "/datastore/" + sid + "/primary/local_clients/" + cid + "/timeline";
        std::vector<std::string> file_diffs = get_file_diffs(path_no_ext);
        // * If no diffs, return empty vec
        size_t n_diffs = file_diffs.size();
        if (n_diffs == 0) {
            return new_messages;
        }
        // * For each file diff line, generate a gRPC Message and add to vec
        for (int i = 0; i < n_diffs; ++i) {
            if (file_diffs[i].size() == 0) {
                continue;
            }

            Message new_msg = entry_str_to_grpc_msg(file_diffs[i]);
            if (new_msg.msg() == "ERROR") {
                continue;
            }
            new_messages.push_back(new_msg);
        }
        return new_messages;
    }
    void init_server_fs(const std::string& sid) {
        /* Called on server registration, initialize our file system */
        std::string sid_path = FS_CWD + "/datastore/" + sid;

        // * Check if an .../$SID/ DNE, create one
        if (!file_exists(sid_path)) {
            if (!std::experimental::filesystem::create_directory(sid_path)) {
                std::cerr << "Error on init_primary_fs create_directory for $SID/\n";
            }	
        }
        

        // * Check if an old .../$SID/primary exists, this may be an old server coming back online
        std::string prim_path = sid_path + "/primary";
        if (!file_exists(prim_path)) {
            // * Make our .../$SID/primary/ if it DNE
            if (!std::experimental::filesystem::create_directory(prim_path)) {
                std::cerr << "Error on init_primary_fs create_directory for $SID/primary\n";
            }	
        }
        
        // * Do the same for .../$SID/primary/local_clients
        std::string loc_cli_path = prim_path + "/local_clients";
        if (!file_exists(loc_cli_path)) {
            // * Make our .../$SID/primary/local_clients
            if (!std::experimental::filesystem::create_directory(loc_cli_path)) {
                std::cerr << "Error on init_primary_fs create_directory for $SID/primary/local_clients\n";
            }
        }
        
        // We should now have .../$SID/primary with or without $SID/secondary without
        // disturbing secondary's files if they exist
    }
    void init_client_fs(const std::string& sid, const std::string& cid) {
        /* 
            On client connection, we want to generate datastore/$SID/primary/local_clients/$CID/
            init_primary_fs should always be called before this
        */

        std::string client_path_ = FS_CWD + "/datastore/" + sid + "/primary/local_clients/" + cid;
        // * If the .../$CID file already exists, do nothing. This may be a client disco that
        //   we want to maintain
        if (!file_exists(client_path_)) {
            // * File DNE, go ahead and make one
            if (!std::experimental::filesystem::create_directory(client_path_)) {
                std::cerr << "Error on init_client_fs make new\n";
            }	
        }
    }
    void write_to_sent_msgs(const std::string& sid, const Message& msg) {
        /*
            Takes a single msg
            Msg must be composed like as a FlaggedDataEntry in the file
        */
        std::string sent_path_ = FS_CWD + "/datastore/" + sid + "/primary/sent_messages.tmp";

        // * Compose like a FlaggedDataEntry
        std::string entry_str = grpc_msg_to_entry_str(msg);

        // * Open sent_messages.tmp in append mode
        std::ofstream data_stream(sent_path_, std::ios::app);

        // * Write message with newline
        data_stream << entry_str << '\n';
    }
    std::vector<std::string> read_global_clients(const std::string& sid) {
        /*
            Read in from global_clients.data, this is used to populate the LIST command and tell
            the user who they can follow
        */
        std::string glob_cli_path_ = FS_CWD + "/datastore/" + sid + "/primary/global_clients.data";
        // * Read into memory all clients in the file
        std::ifstream data_stream(glob_cli_path_);
        std::string line;
        std::vector<std::string> glob_clients;
        while( getline(data_stream, line) ) {
            glob_clients.push_back(line);
        }
        return glob_clients;
    }
    std::vector<Message> read_new_timeline_msgs(const std::string& sid, const std::string& cid) {
        /*
            Read in timeline entries where IOflag=1, flip this flag to zero, these messages will
            then be served to the user. These messages are those placed by the sync service which
            originate from a user outside of this cluster.
        */
        std::string timeline_path_ = FS_CWD + "/datastore/" + sid + "/primary/local_clients/" + cid + "/timeline.data";
        // * If timeline DNE, do nothing
        if (!file_exists(timeline_path_)) {
            return std::vector<Message>();
        }

        // * Read get all entries as Message where IOflag=1, flip to zero and return these Messages to be served to user
        return check_timeline_updates(sid, cid);
    }
    void set_timeline_unread(const std::string& sid, const std::string cid) {
        /* Set all timeline entries as unread so they are reforwarded to user */
        std::string timeline_path_no_ext = FS_CWD + "/datastore/" + sid + "/primary/local_clients/" + cid + "/timeline";
        std::string dpath = timeline_path_no_ext + ".data";
        std::string tpath = timeline_path_no_ext + ".tmp";
        
        if (!file_exists(dpath)) {
            // * Turns out, they didn't even have one...
            return;
        }

        // * Read in from path
        std::vector<std::string> entries;
        std::ifstream data_stream_in(dpath);
        std::string entry;
        while(getline(data_stream_in, entry)) {
            entries.push_back(entry);
        }

        // * Make all IOflag=1
        for (std::string& e: entries) {
            e[0] = '1';
        }

        // * Write to tempfile
        std::ofstream data_stream_out(tpath);
        for (const std::string& e: entries) {
            data_stream_out << e << "\n";
        }

        // * Rename to .data
        std::rename(tpath.c_str(), dpath.c_str());
    }
}   // end namespace PrimaryServer

namespace SecondaryServer {
    
    /*
        If we wanted distributed fault tolerance we would issue RPCs or local reads to 
        back up the database. We leave this undone for now because we treat this database
        like its own distributed object.

        In the real world, this database would also run a server to received distributed
        reads/writes from our Primary/Secondary server. That would require us not to use 
        an actuall server class, which is another large object to link against, bloating
        our build time further.
    */
    
    }    // end namespace SecondaryServer

}   // end namespace schmokieFS   

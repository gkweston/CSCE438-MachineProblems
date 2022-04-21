/*
The format of this (probably) breaks some header file management best-practices,
but should not break the project. This may be refactored on delivery, or it may
just be kept this way (because it works!)

tsn_database is a pseudoclass to manage our tsn data on disc, it includes 3 
namespaces and a few helpers, all wrapped in the namespace DatabaseIO

namespace SyncService
    Manage the database files for the follower sync service, some of these files
    are shared with PrimaryServer, where they are marked below as
        Read:           S:[R]
        Write:          S:[W]
        Exclusive:      S:<X>       -> where a distributed lock service is required


namespace PrimaryServer
    Manage database files for the initial primary server which is prone to
    faults, files shared with SyncService, are marked below as
        Read:           P:[R]
        Write:          P:[W]
        Exclusive:      P:<X>       -> where a distributed lock service is required        

namespace SecondaryServer
    Manage database files for the backup server which is brought online, reads
    ALL datastore/* files and primarily just calls stat() before copying the
    new version to it's file system directory


TSN FileSystem Schema:

$GLOBAL_CWD/
    datastore/
        $(CLUSTER_ID}/
            ${SERVER_TYPE}/
                global_clients.data         P:[R], S:[W]
                local_clients/
                    ${CID}/
                        timeline.data       P:[RW]<X>, S:[W]<X>
                        sent_messages.tmp   P:[W]<X>, S:[R]<X>
                        following.data      P:[W], S:[R]
                        followers.data      P:[WR]<X>, S:[WR]<X>

Each [R] technically writes a file, as it needs to flip the 1st io_flag byte
for each line, so it doesn't read this line again.

io_flag is only used for PrimaryServer, and indicates a write has happened by
another proccess which should be propogated by the server. After propogation,
flip this leading byte to 0. SyncService now checks if fwds exists by checking
if .../$CID/sent_messages.tmp exists, deleting after fwd. SecondaryServer simply
calls stat() to copy diff'd files.

In some cases, we traverse this filesystem to collect metadata. 
    e.g., to read all local clients on the server cluster, SyncService
    reads all filenames in datastore/$CLUSTER_ID/$SERVER_TYPE/local_clients/

tl;dr
    This should be it's own linked library at this point,
    but it's easier to just #include it for now...
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
// using google::protobuf::util;
using csce438::Message;
using csce438::FlaggedDataEntry;

#define FILE_DELIM (std::string("|:|"))

namespace DatabaseIO {

// For file reads
std::string GLOBAL_CWD = std::experimental::filesystem::current_path().string();

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

Message entry_string_to_grpc_message(std::string entry_str) {
    // parts = flag1|flag2|time|cid|msg
    std::vector<std::string> parts = split_string(entry_str);

    Message msg;
    if (parts.size() != 5) {
        std::cout << "entry to gRPC ERR\n";//(!)
        msg.set_msg("ERROR");
        return msg;
    }

    std::time_t ttime = to_time_t(parts[2]);
    Timestamp* timestamp = new Timestamp();
    *timestamp = google::protobuf::util::TimeUtil::TimeTToTimestamp(ttime);
    msg.set_allocated_timestamp(timestamp);
    msg.set_username(parts[3]);
    msg.set_msg(parts[4]);

    return msg;
}

namespace SyncService {

    /*
        Target functionallity
            - Read all clients on this cluster into memory
                @datastore/$SID/$SERVER_TYPE/local_clients
            - For a given CID read all following into memory
                @datastore/$SID/$SERVER_TYPE/local_clients/$CID/following.txt
            - (!)Read updates on a given -- DEPRECATED
                @datastore/$SID/$SERVER_TYPE/local_clients/$CID/sent_messages.data
            - If sent_messages.tmp exists, read and return, delete sent_messages.tmp
                @datastore/$SID/$SERVER_TYPE/local_clients/$CID/sent_messages.data
            - Write received forwards to
                @datastore/$SID/$SERVER_TYPE/local_clients/$CID/timeline.data
            - Write received global users to
                @datastore/$SID/$SERVER_TYPE/global_clients.data
    */

    std::vector<std::string> read_local_cids_from_fs(std::string cluster_sid, std::string stype="primary") {
        // - Read all from memory
        //      @datastore/$SID/$SERVER_TYPE/local_clients
        if (stype != "primary" && stype != "secondary") { //(!)
            std::cout << "ERR INPUT STYPE ON DATABASE::SYNCSERVICE::READ_LOCAL_CID_FROM_FS\n";//(!)
            std::vector<std::string> v;
            return v;
        }

        std::string path_ = GLOBAL_CWD + "/datastore/" + cluster_sid + "/" + stype + "/local_clients";
        std::vector<std::string> local_cids;
        for (const auto& dir : std::experimental::filesystem::directory_iterator(path_)) {
            //* Split and get only the last token which is CID
            std::string cid_token = get_last_token(dir.path(), "/");
            local_cids.push_back(cid_token);
        }
        return local_cids;
    }
    std::vector<std::string> read_following_by_cid(std::string cluster_sid, std::string cid, std::string stype="primary") {
        // - For a given CID read all following into memory
        //     @datastore/$SID/$SERVER_TYPE/local_clients/$CID/following.txt        
        if (stype != "primary" && stype != "secondary") { //(!)
            std::cout << "ERR INPUT STYPE ON DATABASE::SYNCSERVICE::READ_FOLLOWING_BY_CID\n";//(!)
            std::vector<std::string> v;
            return v;
        }

        // * Generate path string 
        std::string path_ = GLOBAL_CWD + "/datastore/" + cluster_sid + "/" + stype + "/local_clients/" + cid + "/following.data";

        // * Read all entries into vector
        std::ifstream ffollowing(path_);
        std::string usr;

        std::vector<std::string> following;
        while(getline(ffollowing, usr)) {
            if (usr.size() > 0 && usr[0] != '\n') { //(!)
                following.push_back(usr);
            }
        }

        // * Pass it back to caller
        return following;
    }
    std::vector<FlaggedDataEntry> check_sent_by_cid(std::string cluster_sid, std::string cid, std::string stype="primary") {
        // - If sent_messages.tmp exists, read and return, delete sent_messages.tmp
        //     @datastore/$SID/$SERVER_TYPE/local_clients/$CID/sent_messages.data
        // Return a vector of FlaggedDataEntry iff there are messages to forward, and no errors occur
        // else return an empty vector
        std::vector<FlaggedDataEntry> entries;
        if (stype != "primary" && stype != "secondary") { //(!)
            std::cout << "ERR INPUT STYPE ON DATABASE::SYNCSERVICE::CHECK_UPDATE\n";//(!)
            return entries;
        } 
        std::string fpath = GLOBAL_CWD + "/datastore/" + cluster_sid + "/" + stype + "/local_clients/" + cid + "/sent_messages.tmp";

        // * If no $CID/sent_messages.tmp, then no new msgs to forward, return empty vec
        if (!file_exists(fpath)) {
            return entries;
        }

        // <X> lock acquire
        // * If $CID/sent_messages.tmp exists, there are new messages to forward
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
        for(const std::string& s: fdata) {
            FlaggedDataEntry e;
            e.set_cid(cid);
            e.set_entry(s);
            entries.push_back(e);
        }

        // * Return vec of these entries
        return entries;
    }
    // std::vector<FlaggedDataEntry> check_update_by_cid(std::string cluster_sid, std::string cid, std::string stype="primary") { // --(!) DEPRECATED
    //     // - Read updates on a given
    //     //     @ datastore/$SID/$SERVER_TYPE/local_clients/$CID/sent_messages.data
    //     if (stype != "primary" && stype != "secondary") { //(!)
    //         std::cout << "ERR INPUT STYPE ON DATABASE::SYNCSERVICE::CHECK_UPDATE\n";//(!)
    //         std::vector<FlaggedDataEntry> v;
    //         return v;
    //     }

    //     /*
    //         Get new messages sent by user corresponding to fname

    //         Only taking fname (not cid) here allows the server/service of whatever
    //         type to iterate all CIDs and generate fnames to check updates from
    //     */

    //     std::string path_no_ext = GLOBAL_CWD + "/datastore/" + cluster_sid + "/" + stype + "/local_clients/" + cid + "/sent_messages";
    //     std::vector<FlaggedDataEntry> new_entries;

    //     // * Get file diff lines
    //     std::vector<std::string> file_diffs = get_file_diffs(path_no_ext);
    //     size_t n_diffs = file_diffs.size();
    //     // * If no diffs, return empty vec
    //     if (n_diffs == 0) {
    //         return new_entries;
    //     }
    //     // * For each file diff line, generate a gRPC FlaggedDataEntry and add to vec
    //     for (int i = 0; i < n_diffs; ++i) {
    //         // Skip incomplete msgs and empty lines
    //         if (file_diffs[i].size() == 0 || file_diffs[i][0] == '\n') {
    //             continue;
    //         }
            
    //         FlaggedDataEntry d_entry;
    //         d_entry.set_cid(cid);
    //         d_entry.set_entry(file_diffs[i]);
    //         new_entries.push_back(d_entry);
    //     }
    //     return new_entries;
    // }
    void write_fwd_to_timeline(std::string cluster_sid, std::string cid, std::string fwd, std::string stype="primary") {
        //  - Writes a single received forward to
        //     @datastore/$SID/$SERVER_TYPE/local_clients/$CID/timeline.data
        //    with IO_flag=1 to indicate to the server it is a new message
        if (stype != "primary" && stype != "secondary") { //(!)
            std::cout << "ERR INPUT STYPE ON DATABASE::SYNCSERVICE::WRITE_FWD_TO_TIMELINE\n";//(!)
            return;
        }

        // * Generate path string corresponding to this user
        std::string path_ = GLOBAL_CWD + "/datastore/" + cluster_sid + "/" + stype + "/local_clients/" + cid + "/timeline.data";

        // * Fwds come as FlaggedDataEntry.entry(), set flag=1
        fwd[0]='1';

        // * Write this to user's timeline file
        std::ofstream user_timeline(path_, std::ios::app);
        user_timeline << fwd << '\n';//(!)

    }
    void write_global_clients(std::string cluster_sid, const std::vector<std::string>& global_clients, std::string stype="primary") {
        // - Write received global users to
        //     @datastore/$SID/$SERVER_TYPE/global_clients.data
        if (stype != "primary" && stype != "secondary") { //(!)
            std::cout << "ERR INPUT STYPE ON DATABASE::SYNCSERVICE::WRITE_GLOBAL_CLIENTS\n";//(!)
            std::cout << "got stype=" << stype << "\n\n";//(!)
            return;
        }

        // * Generate path string
        std::string path_no_ext = GLOBAL_CWD + "/datastore/" + cluster_sid + "/" + stype + "/global_clients";
        std::string tfile = path_no_ext + ".tmp";
        // * Write to temp file
        std::ofstream fglob_client_tmp(tfile);
        for (const std::string& cid: global_clients) {
            fglob_client_tmp << cid << '\n';//(!)
        }
        // * Rename temp file
        std::string dfile = path_no_ext + ".data";
        rename(tfile.c_str(), dfile.c_str());
    }

}   // end namespace SyncService

namespace PrimaryServer {
    
    /*
        Target functionallity
    */

    std::vector<Message> check_update(std::string fname) {

        // (!) fname should inclue *_timeline.dat

        /*
            Get new messages on users timeline corresponding to fname

            Only taking fname (not cid) here allows the server/service of whatever
            type to iterate all CIDs and generate fnames to check updates from
        */
        std::vector<Message> new_messages;

        // * Get file diff liens
        std::vector<std::string> file_diffs = get_file_diffs(fname);
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

            Message new_msg = entry_string_to_grpc_message(file_diffs[i]);
            if (new_msg.msg() == "ERROR") {
                continue;
            }
            new_messages.push_back(new_msg);
        }
        return new_messages;
    }


}   // end namespace PrimaryServer

namespace SecondaryServer {
    
    /*
        Target functionallity
    */

    // secondary flag is always second in ${CID}/set_messages.data, ${CID}/timeline.data
    bool flag_idx = false;

    std::vector<std::string> check_update(std::string fname) {
        /*
            Get lines, where secondary_flag=1, written in:
                primary/${CID}/set_messages.data
                primary/${CID}/timeline.data and
            and write to
                secondary/${CID}/set_messages.data
                secondary${CID}/timeline.data and

            Flip all secondary_flag=1 to 0 when this data
        */

        // (!)(!)(!) Maybe it makes things simpler to just stat() these files, then copy ALL
        // without writing so:
        // A. We don't necessarily need to acquire lock to update secondary files
        // B. Our get file_diffs method is simplified because those files only have 1 flag
        // C. We don't need to add a flag to the ${CID}/following.data files
    }

}   // end namespace SecondaryServer

}   // end namespace DatabaseIO
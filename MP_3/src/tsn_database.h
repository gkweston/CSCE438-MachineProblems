/*
    Database schema
    
    datastore/
        $(CLUSTER_ID}/
            ${SERVER_TYPE}/
                clients.data
                ${CID}/
                    timeline.data
                    sent_messages.data
                    following.data
*/

/*
    We need an interface for doing some database operations

    Datastore will comprise of 3 files

    ${CID}/timeline.data:
        server_flag | secondary_flag | TIME | CID | MSG

    ${CID}/sent_messages.data:
        sync_flag | secondary_flag | TIME | CID | MSG

    ${CID}/following.data
        CID

    sync_flag:
        Server has made a write here, when sync_service reads
        flip this to 0

    server_flag:
        Sync_service has made a write here, when server reads
        flip to 0

    optional secondary_flag:
        Server or sync_service has made a write here, when
        secondary reads flip to 0
*/

/*
    Our read functions will target ONLY lines where sync_flag=1
*/

#include <ctime>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <iomanip>
#include <sstream>

using google::protobuf::Timestamp;
using csce438::Message;

#define FILE_DELIM (std::string("|:|"))


// (!) defined in server namespace
// // writes where sync, secondary flags=1
// void server_write();

// // parses for server_flag=1, return that data
// void server_check_update();

// courtesy of SO
static std::time_t to_time_t(const std::string& str, bool is_dst = false, const std::string& format = "%Y-%b-%d %H:%M:%S") {
    std::tm t = {0};
    t.tm_isdst = is_dst ? 1 : 0;
    std::istringstream ss(str);
    ss >> std::get_time(&t, format.c_str());
    return mktime(&t);
}

std::vector<std::string> split_string(std::string s, std::string delim=FILE_DELIM) {
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

std::vector<std::string> get_file_diffs(std::string fname) {
    
    /*
    Read in user data, anywhere we see sync_flag=1 we'll add this
    line to a vector and return the vector. Overwrite this to sync_flag=0
    */

    std::string line;
    std::vector<std::string> diffd_entries;

    // * Read in the file
    std::ifstream data_stream(fname + std::string(".data"));
    std::ofstream tmp_stream(fname + std::string(".tmp"));
    
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
    std::string tfile = FILENAME + ".tmp";
    std::string dfile = FILENAME + ".data";
    rename(tfile.c_str(), dfile.c_str());

    // * return vector of diff lines
    return diffd_entries;
}

Message entry_string_to_grpc_message(std::string entry_str) {
    // parts = flag1|flag2|time|cid|msg
    std::string parts = split_string(entry_str);

    Message msg
    if (parts.size() != 5) {
        std::cout << "entry to gRPC ERR\n";//(!)
        msg.set_msg("ERROR");
        return msg;
    }

    std::time ttime = to_time_t(parts[2]);
    Timestamp* timestamp = new Timestamp();
    *timestamp = google::protobuf::util::TimeUtil::TimeTToTimestamp(ttime);
    msg.set_allocated_timestamp(timestamp);
    msg.set_username(parts[3]);
    msg.set_msg(parts[4]);

    return msg;
}

namespace SyncService {

    std::vector<Message> check_update(std::string fname) {

        // (!) fname should include *_set_messages.data

        /*
            Get new messages sent by user corresponding to fname

            Only taking fname (not cid) here allows the server/service of whatever
            type to iterate all CIDs and generate fnames to check updates from
        */
        std::vector<Message> new_messages;

        // * Get file diff lines
        std::vector<std::string> file_diffs = get_file_diffs(fname);
        size_t n_diffs = file_diffs.size();
        // * If no diffs, return empty vec
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

}   // end namespace SyncService

namespace PrimaryServer {
    
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


// (!)(!)(!)(!) vvv
// namespace SyncService {

//     struct SentMessageEntry {
//         /*
//             sync_flag | secondary_flag | TIME | CID | MSG
//         */
//         bool sync_flag;
//         bool secondary_flag;
//         std::string timestr;
//         std::string cid;
//         std::string msg;

//         // params = v[0], v[1], ..., v[4]
//         SentMessageEntry(std::string syncf, std::string secf, std::string t, std::string c, std::string m) :
//             timestr(t), cid(c), msg(m) {
//             sync_flag = (syncf == "1") ? true : false;
//             secondary_flag = (secf == "1") ? true : false
//         }

//         Message to_grpc_msg() {
//             // * Gen grpc timestamp
//             std::time_t ttime = to_time_t(timestr);
//             Timestamp* timestamp = new Timestamp();
//             *timestamp = google::protobuf::util::TimeUtil::TimeTToTimestamp(ttime);
//             // * Make grpc msg
//             Message grpc_msg;
//             grpc_msg.set_allocated_timestamp(timestamp);
//             grpc_msg.set_username(cid);
//             grpc_msg.set_msg(msg);
//         }

//         std::string to_str() { //(!)
//             std::string entry_str = "";
//             // * get sync flag
//             entry_str += (sync_flag) ? "1" : "0";
//             entry_str += FILE_DELIM;
//             // * get secdonary flag
//             entry_str += (secondary_flag) ? "1" : "0";
//             entry_string += FILE_DELIM;
//             // * get timestr, cid, msg
//             entry_str += (timestr + FILE_DELIM);
//             entry_str += (cid + FILE_DELIM);
//             entry_str += msg;

//             return entry_str;
//         }
//     };

//     // (!) May want to write a generalizable version of this functiton...
//     // void write_sent_messages(std::string fname, std::vector<SentMessageEntry>const &entries) {
//     //     ofstream out_stream(fname);
//     //     for (int i = 0; i < entries.size(); ++i) {
//     //         SentMessageEntry e = entries[i];
//     //         out_stream << e.to_str() << '\n';
//     //     }
//     // }

//     // Issued on ${CID}_sent_message.data
//     // Should we generalize this to Prim/Sec server updates as well -- we would just check different flags...
//     std::vector<Message> check_update(std::string fname) {
//         /*
//         Parse the ${CID}_sent_message.data file
//         return a vector<Message> where sync_flag=1
//         set these entries to sync_flag=0
//         */

//         std::vector<Message> new_messages;
//         std::string line;
//         std::fstream file_stream(fname);
//         std::vector<SentMessageEntry*> file_entries;

//         // * Read in all file lines into memory
//         while(getline(file_stream, line)) {
//             // parts = { sync_flag, secondary_flag, TIME, CID, MSG }
//             std::vector<std::string> parts = split_string(line);
            
//             // * Add complete entries to file_entries vec
//             if (parts.size() == 5) {
//                 SentMessageEntry* e = new SentMessageEntry(parts[0], parts[1], parts[2], parts[3], parts[4]);
//                 file_entries.push_back(e);
//             }
//         }
//         // * Retrieve all lines where sync_flag=1
//         std::vector<Message> new_messages;
//         for (int i = 0; i < file_entries.size(); ++i) {
//             SentMessageEntry* e = file_entries[i];
//             if (e->sync_flag) {
//                 Message grpc_msg = e->to_grpc_msg();
//                 new_messages.push_back(grpcs_msg);
//                 e->sync_flag = false;
//             }
//         }
//         // * Write all messages back to file, so sync_flag=0
//         // For now we won't use a function, to keep from dealing with const& stuff
//         // write_sent_messages(fname, file_entries);
//         for (int i = 0; i < entries.size(); ++i) {
//             SentMessageEntry* e = entries[i];
//             file_stream << e->to_str() << '\n';
//         }
//         file_stream.close();

//         // * Return our new message so they can be propogated across other server clusters
//         return new_messages;
//     }
// } // end SyncService namespace

// namespace ServerPrimary {

// } // end namespace ServerPrimary

// namespace ServerSecondary {
//     // We'll do this on next step...
// } // end namespace ServerSecondary
// 
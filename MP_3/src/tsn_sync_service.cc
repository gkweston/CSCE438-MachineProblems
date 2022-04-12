/*
    Start this by reading in all messages for ${UID}.txt
    keep { TIME | UID | MSG } in memory

    we'll keep this in memory until it's sent on the wire

    use stat(), or similar, to check when to read in mem

    keep TIME last_data_diff on hand

    VERY sloppy, hacky, needs polishing all over
*/

#include <ctime>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <iomanip>
#include <sstream>


#include <google/protobuf/util/time_util.h>
#include "sns.grpc.pb.h"

using google::protobuf::Timestamp;
using csce438::Message;

#define FILE_DELIM (std::string("|:|"))

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

std::vector<Message> read_timeline_data(std::string fname) {
    // Parse timeline datastore file into a vector of grpc Messages
    std::vector<Message> messages;
    std::string line;
    std::vector<std::string> newest_twenty;
    std::ifstream in_stream(fname);
    // Parse input file
    while(getline(in_stream, line)){
        Message msg;
        // Parse line in file
        std::vector<std::string> parts = split_string(line);
        // Skip file lines w/o TIME|CID|MSG
        if (parts.size() != 3) {
            continue;
        }
        // Generate grpc Timestamp
        std::time_t timestmp = to_time_t(parts[0]);
        Timestamp* timestamp = new Timestamp();
        *timestamp = google::protobuf::util::TimeUtil::TimeTToTimestamp(timestmp);
        // Set Message descriptors and add to vec
        msg.set_allocated_timestamp(timestamp);
        msg.set_username(parts[1]);
        msg.set_msg(parts[2]);
        messages.push_back(msg);
    }
    return messages;
}

void check_update();
void get_sync_service_addr();
void write_user_timeline(std::string cid);

int main() {
    std::cout << "Testing message read-ins\n";
    std::vector<Message> messages = read_timeline_data("test_input.txt");
}
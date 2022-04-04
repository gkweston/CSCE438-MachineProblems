/*
Diffs required:
	Step 1.
	* Add coordinator stub
	* Send hostname, port info to coordinator on init

	Step 2.
	* Assign enum for PRIMARY, SECONDARY at execution
	* Establish connection w/ coordinator, send HRTBT on this channel
	* Create ./datastore/TYPE_ID/[context.data, user_timelines.data]
	* Change IO to write properly to files
	* Establish connection w/ coordinator, send LOCK_ACQUIRE, LOCK_RELEASE on this channel
*/

/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <ctime>

#include <google/protobuf/timestamp.pb.h>
#include <google/protobuf/duration.pb.h>

#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <stdlib.h>
#include <unistd.h>
#include <google/protobuf/util/time_util.h>
#include <grpc++/grpc++.h>

#include "sns.grpc.pb.h"
#include "coordinator.h"

using google::protobuf::Timestamp;
using google::protobuf::Duration;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerReader;
using grpc::ServerReaderWriter;
using grpc::ServerWriter;
using grpc::Status;
using grpc::ClientContext;
using csce438::Message;
using csce438::ListReply;
using csce438::Request;
using csce438::Reply;
using csce438::SNSService;
using csce438::Registration;
using csce438::SNSCoordinatorService;

#define DEFAULT_HOST (std::string("0.0.0.0"))
#define SUCCESS 					("SUCCESS")
#define FAILURE_ALREADY_EXISTS 		("FAILURE_ALREADY_EXISTS")
#define FAILURE_NOT_EXISTS 			("FAILURE_NOT_EXISTS")
#define FAILURE_INVALID_USERNAME 	("FAILURE_INVALID_USERNAME")
#define FAILURE_INVALID 			("FAILURE_INVALID")
#define FAILURE_UNKNOWN 			("FAILURE_UNKNOWN")

struct Client {
	std::string username;
	bool connected = true;
	int following_file_size = 0;
	std::vector<Client*> client_followers;
	std::vector<Client*> client_following;
	ServerReaderWriter<Message, Message>* stream = 0;
	bool operator==(const Client& c1) const{
		return (username == c1.username);
	}
};

//Vector that stores every client that has been created
std::vector<Client> client_db;

//Helper function used to find a Client object given its username
int find_user(std::string username){
	int index = 0;
	for(Client c : client_db){
		if(c.username == username)
			return index;
		index++;
	}
	return -1;
}

class SNSServiceImpl final : public SNSService::Service {
  
	Status List(ServerContext* context, const Request* request, ListReply* list_reply) override {
		Client user = client_db[find_user(request->username())];
		int index = 0;
		for(Client c : client_db){
			list_reply->add_all_users(c.username);
		}
		std::vector<Client*>::const_iterator it;
		for(it = user.client_followers.begin(); it!=user.client_followers.end(); it++){
			list_reply->add_followers((*it)->username);
		}
		return Status::OK;
	}

	Status Follow(ServerContext* context, const Request* request, Reply* reply) override {
		std::string username1 = request->username();
		std::string username2 = request->arguments(0);
		int join_index = find_user(username2);
		if(join_index < 0 || username1 == username2)
			reply->set_msg(FAILURE_INVALID_USERNAME);
		else{
			Client *user1 = &client_db[find_user(username1)];
			Client *user2 = &client_db[join_index];
			if(std::find(user1->client_following.begin(), user1->client_following.end(), user2) != user1->client_following.end()){
				reply->set_msg(FAILURE_ALREADY_EXISTS);//(!)"Join Failed -- Already Following User"
				return Status::OK;
			}
			user1->client_following.push_back(user2);
			user2->client_followers.push_back(user1);
			reply->set_msg(SUCCESS);
		}
		return Status::OK; 
	}

	Status UnFollow(ServerContext* context, const Request* request, Reply* reply) override {
		std::string username1 = request->username();
		std::string username2 = request->arguments(0);
		int leave_index = find_user(username2);
		if(leave_index < 0 || username1 == username2)
			reply->set_msg(FAILURE_INVALID_USERNAME);
		else{
			Client *user1 = &client_db[find_user(username1)];
			Client *user2 = &client_db[leave_index];
			if(std::find(user1->client_following.begin(), user1->client_following.end(), user2) == user1->client_following.end()){
				reply->set_msg(FAILURE_NOT_EXISTS);//(!)"Leave Failed -- Not Following User"
				return Status::OK;
			}
			user1->client_following.erase(find(user1->client_following.begin(), user1->client_following.end(), user2)); 
			user2->client_followers.erase(find(user2->client_followers.begin(), user2->client_followers.end(), user1));
			reply->set_msg(SUCCESS);
		}
		return Status::OK;
	}
  
	Status Login(ServerContext* context, const Request* request, Reply* reply) override {
		Client c;
		std::string username = request->username();
		int user_index = find_user(username);
		if(user_index < 0){
			c.username = username;
			client_db.push_back(c);
			reply->set_msg(SUCCESS);
		}
		else{ 
			Client *user = &client_db[user_index];
			if(user->connected)
				reply->set_msg(FAILURE_INVALID_USERNAME);
			else{
				std::string msg = "Welcome Back " + user->username;
				reply->set_msg(msg);
				user->connected = true;
			}
		}
		return Status::OK;
	}

	Status Timeline(ServerContext* context, 
	ServerReaderWriter<Message, Message>* stream) override {
		Message message;
		Client *c;
		while(stream->Read(&message)) {
			std::string username = message.username();
			int user_index = find_user(username);
			c = &client_db[user_index];

			//Write the current message to "username.txt"
			std::string filename = username+".txt";
			std::ofstream user_file(filename,std::ios::app|std::ios::out|std::ios::in);
			google::protobuf::Timestamp temptime = message.timestamp();
			std::string time = google::protobuf::util::TimeUtil::ToString(temptime);
			std::string fileinput = time+" :: "+message.username()+":"+message.msg()+"\n";
			//"Set Stream" is the default message from the client to initialize the stream
			if(message.msg() != "Set Stream")
				user_file << fileinput;
			//If message = "Set Stream", print the first 20 chats from the people you follow
			else{
				if(c->stream==0)
					c->stream = stream;
				std::string line;
				std::vector<std::string> newest_twenty;
				std::ifstream in(username+"following.txt");
				int count = 0;
				//Read the last up-to-20 lines (newest 20 messages) from userfollowing.txt
				while(getline(in, line)){
					if(c->following_file_size > 20){
						if(count < c->following_file_size-20){
							count++;
							continue;
						}
					}
					newest_twenty.push_back(line);
				}
				Message new_msg; 
				//Send the newest messages to the client to be displayed
				for(int i = 0; i<newest_twenty.size(); i++){
					new_msg.set_msg(newest_twenty[i]);
					stream->Write(new_msg);
				}    
				continue;
			}
			//Send the message to each follower's stream
			std::vector<Client*>::const_iterator it;
			for(it = c->client_followers.begin(); it!=c->client_followers.end(); it++){
				Client *temp_client = *it;
				if(temp_client->stream!=0 && temp_client->connected)
					temp_client->stream->Write(message);
				//For each of the current user's followers, put the message in their following.txt file
				std::string temp_username = temp_client->username;
				std::string temp_file = temp_username + "following.txt";
				std::ofstream following_file(temp_file,std::ios::app|std::ios::out|std::ios::in);
				following_file << fileinput;
				temp_client->following_file_size++;
				std::ofstream user_file(temp_username + ".txt",std::ios::app|std::ios::out|std::ios::in);
				user_file << fileinput;
			}
		}
		//If the client disconnected from Chat Mode, set connected to false
		c->connected = false;
		return Status::OK;
	}

private:
	std::string coordinator_addr;
	std::string cluster_sid;
	std::string port;
	ServerType type_at_init;

	std::unique_ptr<SNSCoordinatorService::Stub> coord_stub_;

	void RegisterWithCoordinator();
public:
	// Save sid, hostname, port and initialize coordinator RPC, then call the coordinator register function
	SNSServiceImpl(std::string coord_addr, std::string p, std::string sid, ServerType t);
};

SNSServiceImpl::SNSServiceImpl(std::string coord_addr, std::string p, std::string sid, ServerType t) {
	// Server descriptors
	coordinator_addr = coord_addr;
	port = p;
	cluster_sid = sid;

	// This may change as server(s) fault, but we'll track what this server was
	// spun up as [PRIMARY|SECONDARY]
	type_at_init = t;
	
	// Generate coordinator stub here so we can reuse
	coord_stub_ = std::unique_ptr<SNSCoordinatorService::Stub>(
		SNSCoordinatorService::NewStub(
			grpc::CreateChannel(
				coordinator_addr, grpc::InsecureChannelCredentials()
			)
		)
	);

	// Send registration message
	RegisterWithCoordinator();
}
void SNSServiceImpl::RegisterWithCoordinator() {
	/* Register server w/ coordinator, add me to the routing table(s)! */

	// Fill RPC metadata and reg msg
	Registration reg;
	reg.set_sid(cluster_sid);
	reg.set_hostname(DEFAULT_HOST);
	reg.set_port(port);
	
	std::string server_type_str = type_to_string(type_at_init);
	std::cout << "Registering as type=" << server_type_str << '\n';//(!)
	reg.set_type(server_type_str);

	Reply repl;
	ClientContext ctx;

	// Dispatch registration RPC
	Status stat = coord_stub_->RegisterServer(&ctx, reg, &repl);
	if (!stat.ok())
		std::cerr << "Server registration error for:\nsid=" << cluster_sid << "\ntype=" << type_at_init << "\n";
	else
		std::cout << repl.msg() << std::endl;//(!)
	
}

void RunServer(std::string coord_addr, std::string sid, std::string port_no, ServerType type) {
	// Spin up server instance
	std::string server_address = DEFAULT_HOST + ":" + port_no;
	// ------->>>
	// SNSServiceImpl service;
	// OR
	SNSServiceImpl service(coord_addr, port_no, sid, type);
	// <<<-------

	ServerBuilder builder;
	builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
	builder.RegisterService(&service);
	std::unique_ptr<Server> server(builder.BuildAndStart());
	std::cout << "Server listening on " << server_address << std::endl;

	server->Wait();
}

int main(int argc, char** argv) {

	/*
	Simplified args:
		-c <coordinatorIP>:<coordinatorPort>
		-p <port>
		-i <serverID>		should be a string of integers
		-t <type>			[master|primary] or anything else to assign as secondary
	*/
	if (argc == 1) { //(!)
		std::cout << "Calling convention for server:\n\n";
		std::cout << "./tsn_server -c <coordIP>:<coordPort> -p <serverPort> -i <serverID> -t <serverType>\n\n";
		return 0;
	}
  
	std::string port = "3010";
	std::string coord;
	std::string serverID;
	ServerType type;

	int opt = 0;
	while ((opt = getopt(argc, argv, "c:p:i:t:")) != -1){
		switch(opt) {
			case 'c':
				coord = optarg;
				break;
			case 'p':
				port = optarg;
				break;
			case 'i':
				serverID = optarg;
				break;
			case 't':
				type = parse_type(std::string(optarg));
				break;
			default:
				std::cerr << "Invalid Command Line Argument\n";
		}
	}

	if (type == ServerType::INVALID) {
		std::cout << "Server type error\n";//(!)
		return 1;
	}
	
	RunServer(coord, serverID, port, type);
	return 0;
}

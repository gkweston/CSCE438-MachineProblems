# CSCE438 MP3 — Distributed & Fault Tolerant Networking Service
## Description
A distributed network which is designed to keep a custom filesystem depending on the connected clients. As long as a secondary server is spun up, the network can withstand network partitions without any loss of availability. This availabillity and partition tolerance comes at the cost of consistency. For example, messages originating from the same cluster are guarenteed to be properly ordered, however messages originating from multiple clusters are not.

SyncServices also work in the background to propogate data, which is only limited to the frequency the Sync Service is called.

e.g., increasing SyncService frequency to something like 500ms gives the appearance that users are all on the same cluster. A frequency faster than this makes network propogation appear instantaneous!

Default values for these parameters are set according to the assignment instructions.


## Compilation & Use

    # Build binaries, prebuilt are located in bin/
    make

    # To run
    ./tsn_coordinator   -p <port>
        Optional:       -c [to clear datastore]

    ./tsn_server    -c <coordIP>:<coordPort>
                    -p <serverPort>
                    -i <serverID>
                    -t <primary|secondary>

    ./tsn_sync_service  -c <coordIP>:<coordPort>
                        -s <serverID>
                        -p <port>
        Optional:       -q <refreshFrequencyMilli>

    ./tsn_client    -c <coordIP>:<coordPort>
                    -p <clientPort>
                    -i <clientID>
    



## Datastore schema
    datastore/
        ${CLUSTER_ID}/
            sent_messages.tmp
            ${SERVER_TYPE}/
                global_clients.data
                local_clients/
                    ${CID}/
                        timeline.data
                        following.data

All files in the datastore or persistant, except for sent_messages.tmp, which is deleted each time it is consumed by a Sync Service.

## schmokieFS
The Schmokie File System is a custom pseudo-class which acts to represent a distributed database that can operate on different machines. Currently, it is comprised of nested namespaces which represent different database object operations.

Future additions would see this become a fully-fledged server which registers with the coordinator and exports functions to facilitate a distributed lock service and make backups on different machines. This could be done by:
    
1. Adding schmokieFS rpc's to the coordinator.
2. Encapsulating different functions in classes that are inhereted from the schmokieFS class (i.e. SyncService, PrimaryServer, SecondaryServer).
3. Adding methods which issue lock/lease requests to the coordinator, and block until they return. This would allow the locking process to be completely abstracted away from the caller.

Effectively, any service in the system would issue a schmokieFS read/write, which can be spawned in a seperate thread. schmokieFS would then issue a lock request to the coordinator, continue once it is fulfilled, and return to the caller after releasing. Pretty cool stuff!


### Misc

Member and helper functions

    CamelCase is used when a function issues an RPC (immediately, or downstream)
    snake_case is used otherwise

A `#define DEBUG` is included in each, not necessarily because this is good practice, but because:
    
    a. It is helpful to show others how network propogation occurs
    b. It will be useful if I decide to expand this (and schmokieFS) in the future

Other docs are in the code!

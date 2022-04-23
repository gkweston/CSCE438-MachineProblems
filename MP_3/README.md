Directory structuring for binaries (excluding sourcecode)

    src/
        tsn_client
        tsn_server
        tsn_coordinator
        tsn_sync_service
        datastore/
            ${CLUSTER_ID}/
                ${SERVER_TYPE}/
                    global_clients.data
                    local_clients/
                        ${CID}/
                            timeline.data
                            *sent_messages.tmp
                            following.data

```
Notes:
    * All server/client IDs are assigned on execution (no uuidgen)
    * Change tsc -> tsn_client
    * Change tsd -> tsn_server

For member and helper functions
        CamelCase is used when a function issues an RPC (immediately, or downstream)
        snake_case is used otherwise
```

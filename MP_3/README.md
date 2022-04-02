Directory structuring for binaries (excluding sourcecode)

src/
    tsc
    tsd
    tsn_coordinator
    tsn_sync_service
    datastore/
        ${SERVER_TYPE}_${SERVER_ID}/
            server_context.data
            user_timelines.data
        ...
            ...

Notes:
    * All server/client IDs are assigned on execution (no uuidgen)
    * Change tsc -> tsn_client
    * Change tsd -> tsn_server
    

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

```
Notes:
    * All server/client IDs are assigned on execution (no uuidgen)
    * Change tsc -> tsn_client
    * Change tsd -> tsn_server

For member and helper functions
        CamelCase is used when a function issues an RPC (immediately, or downstream)
        snake_case is used otherwise
```

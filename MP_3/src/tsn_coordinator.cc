/*
Notes:
    Step 1
    * Wait for connection requests from clients
    * Route them via CID % 3
    * Keep routing tables for
        SID, PORT_primary, STATUS
        SID, PORT_secondary, STATUS
        SID, PORT_sync_service, STATUS
    * Keep user tables for
        CID, STATUS_primary, STATUS_secondary
        

    Step 2
    * Init channel for HRTBT, if <20 seconds (2 cycles) of heartbeats are missed
        Assign secondary server as active
        Send REDIRECT to all clients to connect with this server
    * Init channel for LOCK_ACQUIRE, LOCK_RELEASE and distribute locks on this
    
*/
/*
    We need an interface for doing some database operations

    Datastore will comprise of 3 files

    ${CID}_timeline.data:
        TIME | CID | MSG | server_flag[0 or 1]

    ${CID}_sent_messages.data:
        TIME | CID | MSG | sync_flag[0 or 1]

    ${CID}_following.data
        CID

    sync_flag:
        Server has made a write here, when sync_service reads
        flip this to 0

    server_flag:
        Sync_service has made a write here, when server reads
        flip to 0

    *optional secondary_flag:
        Server or sync_service has made a write here, when
        secondary reads flip to 0-
*/

/*
    Our read functions will target ONLY lines where sync_flag=1
*/

// writes where sync, secondary flags=1
void server_write();
// parses for server_flag=1, return that data
void server_check_update();
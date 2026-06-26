
// service type identifiers

#ifndef SERVICE_TYPE_IDS_H
#define SERVICE_TYPE_IDS_H

enum SERVICE_TYPE {
        SERVICE_NONE=0,
        // SERVICE-1: EXIST; proof of existence for documents
        SERVICE_EXIST,
        // SERVICE-2: VOTE; voting
        SERVICE_VOTE,
        // SERVICE-3: XCOUT; cross chain transfer out
        SERVICE_XCOUT,
        // SERVICE-4: XCIN; cross chain transfer in
        SERVICE_XCIN
};

#endif

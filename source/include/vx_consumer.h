#ifndef VX_CONSUMER_H_
#define VX_CONSUMER_H_

#include <tivx_utils_ipc_ref_xfer.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

    typedef struct
    {
        vx_graph    graph;
        char        name[VX_MAX_CONSUMER_NAME];
        char        access_point_name[VX_MAX_ACCESS_POINT_NAME];
        pthread_t   receiver_thread;
    } cons_internal_data_t;

    typedef struct _vx_consumer{

        tivx_reference_t                  base;
        cons_internal_data_t              internals;
        vx_dispatch_cb_t                  dispatch_cb;
    }tivx_consumer_t;

#ifdef __cplusplus
}
#endif

#endif // VX_CONSUMER_H_

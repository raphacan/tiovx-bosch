
#ifndef VX_PRODUCER_IPPC_H_
#define VX_PRODUCER_IPPC_H_

#include <utils/ippc/include/app_ippc.h>
#include "vx_gc_config.h"
#include "vx_producer.h"

#ifdef __cplusplus
extern "C" {
#endif

/*! \brief Producer IPPC specific params
 * \ingroup group_vx_producer
 */
typedef struct _vx_gc_prod_params_t
{      
    /*! \brief Contains ippc port configuration */
    SIppcPortMap        ippc_port[IPPC_PORT_COUNT];
    /*! \brief rate at which producer polls for new consumer during startup */
    vx_uint32           connection_check_polling_time;
} vx_gc_producer_params_t;

/*! \brief The producer message content via IPPC
 * \ingroup group_vx_producer
 */
typedef struct
{
    /*! \brief The buffer information exchanged b/w producer and consumer */
    buffer_info_t buffer_info;
    /*! \brief Contains producer metadata */
    vx_uint8 metadata_buffer[VX_GC_MAX_META_SIZE];
    /*! \brief number of producer references */
    vx_uint8 num_refs;
    /*! \brief Array used to store intermediate IPC messages */
    tivx_utils_ref_ipc_msg_t ref_export_handle[VX_GC_MAX_NUM_REFS][VX_GC_MAX_NUM_ITEMS];

} producer_msg_content_t;

/*! \brief Backchannel information from consumer
 * \ingroup group_vx_producer
 */
typedef struct 
{
    /*! \brief Indicates the producer-consumer connection status */
    producer_client_state_e state;

    /*! \brief consumer id, used to distinguish consumers on app level */
    vx_uint8                consumer_id;

    /*! \brief Thread to receive backchannel information from consumer */
    pthread_t               bck_thread;

    /*! \brief Contains receiver context */
    SIppcReceiverContext    m_receiver_ctx;
} producer_bckchannel_t;

/*! \brief Producer object internal state
 * \ingroup group_vx_producer
 */
typedef struct _vx_producer
{
    producer_base_struct_t prod_base;
    /*! \brief Stores consumers backchannel information */;
    producer_bckchannel_t  consumers_list[VX_GC_NUM_CLIENTS];
    /*! \brief Poll for new clients during startup */
    pthread_t              connection_check_thread;
    /*! \brief rate at which producer polls for new consumer during startup */
    vx_uint32              connection_check_polling_time;
    /*! \brief exit condition for polling connection check thread */
    vx_bool                connection_check_polling_exit;
    /*! \brief Contains shmem context */
    SIppcShmemContext      m_shmem_ctx;
    /*! \brief Contains sender context */
    SIppcSenderContext     m_sender_ctx;
    /*! \brief Contains ippc port configuration */
    SIppcPortMap           ippc_port[IPPC_PORT_COUNT];
} vx_producer_t;

vx_status ownInitProducerObjectIppc(vx_producer producer, const vx_producer_params_t* params);

vx_status producerStartIppc(vx_producer producer);

vx_status releaseProducerIppc(vx_producer* producer);

#ifdef __cplusplus
}
#endif

#endif /*VX_PRODUCER_IPPC_H_*/

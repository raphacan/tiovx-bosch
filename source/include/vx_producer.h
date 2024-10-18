#ifndef VX_PRODUCER_H_
#define VX_PRODUCER_H_

#define IPPC_SHEM_ENABLED

#ifdef __cplusplus
extern "C" {
#endif    
    
    #define MAX_NB_OF_CONSUMERS   4u
    #define IPPC_CONFIG_MAX_PORTS 8u

    typedef enum
    {
        NOT_CONNECTED    = 0U,
        INIT             = 1U,
        RUNNING          = 3U,
        STOPPED          = 4U
    } consumer_state_t;

    typedef struct
    {
        consumer_state_t m_state;
        pthread_t        m_thread;
        uint16_t         m_consumer_num;
    } producer_backchannel_cxt_t;

    //producer->consumers message (1->N)
    typedef struct
    {
        // used at runtime, release
        int32_t   ovx_buffer_index;
        uint32_t* ovx_supplementary_data;
        uint32_t  last_buffer;
    } producer_generic_payload_t;

    //consumer->producer message (1->1)
    typedef struct
    {
        uint32_t ovx_buffer_index;
        uint32_t last_buffer;
    } consumer_generic_payload_t;

    typedef struct
    {
        // number of total object array items; set to zero if reference is not object array
        uint8_t num_items;
        // number representing the element index for object array; set to zero if reference is not an object array item
        uint8_t item_index;
        // flag to indicate if this is the last reference to be exchanged with the consumer
        uint8_t last_reference;
        tivx_utils_ref_ipc_msg_t ipc_message_item[OVXGW_MAX_NUM_REFS];
    } ovx_buffer_meta_payload_t;
    
    typedef struct
    {
        vx_graph                   graph;
        char                       name[VX_MAX_PRODUCER_NAME];
        char                       access_point_name[VX_MAX_ACCESS_POINT_NAME];
        /* we configure here the max number of consumer
           this can differ from the communication protocol itself --> to check */
        vx_uint16                  max_consumers;
        vx_uint16                  nb_receiver_ready;
        producer_backchannel_cxt_t consumer_list[MAX_NB_OF_CONSUMERS];

        pthread_t broadcast_thread;
        vx_uint32 sequence_num;
        vx_uint32 total_sequences;
    } internal_data_t;

    typedef struct _vx_producer{

        tivx_reference_t                  base;
        internal_data_t                   internals;
        vx_graph_parameter_queue_params_t ref_to_export;
        vx_streaming_cb_t                 streaming_cb;
        vx_notify_cb_t                    notify_cb;
#ifdef IPPC_SHEM_ENABLED
        ippc_shmem_sontext_t              shem_ctx;
        ippc_sender_context_t             shem_sender_ctx;
        ippc_receiver_context_t           shem_receiver_ctx;
#elif IPPC_SOCKET_ENABLED

#endif
    }tivx_producer_t;

#ifdef __cplusplus
}
#endif

#endif //VX_PRODUCER_H_

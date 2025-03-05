typedef struct
{
    /*! \brief Indicates id of the buffer to be exchanged with the consumer */
    vx_int32  buffer_id;
    /*! \brief Indicates to receivers whether current frame shall be consumed or not */
    vx_uint32  mask;
    /*! \brief flag to indicate if this is the last reference to be exchanged with the consumer */
    vx_uint32 last_buffer;
    /*! \brief flag to inform consumer whether previous frame has been dropped by producer */
    vx_uint8 last_frame_dropped;
}header_t;

typedef struct
{
    /*! \brief flag set when metadata can be read by consumer */
    vx_uint8 metadata_valid;
    /*! \brief size of metadata */
    size_t metadata_size;
    /*! \brief Contains producer metadata */
    vx_uint8 metadata_buffer[VX_GW_MAX_META_SIZE];
}metatdata_t;

typedef struct
{
    /*! \brief number of total object array items; set to zero if reference is not object array */
    vx_uint8 num_items;
    /*! \brief number of producer references */
    vx_uint8 num_refs;
    /*! \brief Array used to store intermediate IPC messages */
    tivx_utils_ref_ipc_msg_t ref_export_handle[VX_GW_MAX_NUM_REFS][VX_GW_MAX_NUM_ITEMS];
}ipc_handle_t;


typedef struct
{
   header_t     header; --> used by both ippc and socket
   metatdata_t  metadata; --> used by both ippc and socket
   ipc_handle_t ipc_handles; --> used only by ippc

} vx_prod_msg_content_t;

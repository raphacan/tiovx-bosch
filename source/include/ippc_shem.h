//=============================================================================
//  C O P Y R I G H T
//-----------------------------------------------------------------------------
/// @copyright (c) 2024 by Robert Bosch GmbH. All rights reserved.
//
//  The reproduction, distribution and utilization of this file as
//  well as the communication of its contents to others without express
//  authorization is prohibited. Offenders will be held liable for the
//  payment of damages. All rights reserved in the event of the grant
//  of a patent, utility model or design.
//=============================================================================
//  P R O J E C T   I N F O R M A T I O N
//-----------------------------------------------------------------------------
//     Projectname: IPPC
//  Target systems: cross platform
//       Compilers: ISO C compliant or higher
//=============================================================================
//  I N I T I A L   A U T H O R   I D E N T I T Y
//-----------------------------------------------------------------------------
//        Name:
//  Department: XC-AS/EPO3
//=============================================================================
/// @file  ovxgw_common.h
//=============================================================================

#ifndef __OVXGW_COMMON_H
#define __OVXGW_COMMON_H

#include <vx_internal.h>
#include "ippc.h"

typedef enum
{
    PORT_TYPE_BROADCAST,
    PORT_TYPE_UNICAST,
} EIppcPortType;

typedef struct {
    SIppcShmHandler m_shmem_handler;
    SIppcRegistry   m_registry;

    size_t        m_msgsize_broadcast;
    size_t        m_msgsize_unicast;
    uint32_t      m_num_receivers;

    uint32_t      m_num_ports;
    SIppcPortMap  m_ports[2*MAX_NB_OF_CONSUMERS];
    /*trigger for the receivers */
    SIppcSync  *  m_sync[IPPC_CONFIG_MAX_PORTS];
} SIppcShmemContext;
typedef struct {
    SIppcPortMap    m_port_map;
    size_t          m_msg_size;    
    uint16_t        m_receivers_id;
    /* IPPC object itself */
    SIppcSender     m_sender;
} ippc_sender_context_t;

typedef struct {
    SIppcPortMap    m_port_map;
    size_t          m_msg_size;
    /* IPPC object itself */
    SIppcReceiver   m_receiver;
    /* sync object the receiver is waiting on */
    SIppcSync  *          m_sync;
    ippc_receive_callback m_client_handler;
    void*                 m_application_ctx;
} ippc_receiver_context_t;

//producer->consumers message (1->N)
typedef struct
{
    /* the generic buffer constain the buffer ID and additional meta/supplementary */
    producer_generic_payload_t generic;
    /* specific to IPPC SHEM 
       used only at init to specify:
       - the port ID, they are fixed by the IPPC itself per application
       - the consumer number: id incremented every time a new connection has been established
       - the openvx buffer references for the export itself
    */
    uint32_t m_backchannel_port; 
    uint32_t m_consumer_num;
    ovx_buffer_meta_payload_t m_ovx_buffer_meta[BUFFER_POOL_SIZE * NB_OF_OVX_REFERENCE];
} producer_payload_t;

#define IPPC_SHEM_PORT_COUNT (MAX_NB_OF_CONSUMERS+1u)
const SIppcPortMap g_ippc_port_configuration[IPPC_SHEM_PORT_COUNT] = {{0, PORT_ID_FWD, PORT_TYPE_BROADCAST},
                                                                    {0, PORT_ID_RECEIVER_1_BCK, PORT_TYPE_UNICAST},
                                                                    {1, PORT_ID_RECEIVER_2_BCK, PORT_TYPE_UNICAST},
                                                                    {2, PORT_ID_RECEIVER_3_BCK, PORT_TYPE_UNICAST},
                                                                    {3, PORT_ID_RECEIVER_4_BCK, PORT_TYPE_UNICAST}};

/* producer (in) */
/* f_shmem (out) */
vx_status ippc_shmem_init(const vx_producer producer, SIppcShmemContext* f_shmem);

void ippc_shmem_deinit(SIppcShmemContext* const f_shmem, const char* const f_shm_name);

vx_status ippc_shem_payload_pointer(ippc_sender_context_t* const f_sender_context, const size_t f_payload_size, void** f_payload);

vx_status ippc_shem_send(ippc_sender_context_t* const f_sender_context);

vx_status ippc_shem_receive(ippc_receiver_context_t* const f_receiver_context);

#endif

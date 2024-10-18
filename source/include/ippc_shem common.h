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

#ifndef __IPPC_SHEM_COMMON_H
#define __IPPC_SHEM_COMMON_H

#include "ippc_shm/ippc_shm.h"

typedef enum
{
    PORT_TYPE_BROADCAST,
    PORT_TYPE_UNICAST,
} EIppcPortType;

//producer->consumers message (1->N)
typedef struct
{
    //specific to IPPC SHEM
    uint32_t m_backchannel_port; 
    uint32_t m_consumer_num;
    uint32_t m_ovx_buffer_meta;
} producer_shem_payload_t;

typedef struct {
    SIppcShmHandler m_shmem_handler;
    SIppcRegistry   m_registry;

    size_t        m_msgsize_broadcast;
    size_t        m_msgsize_unicast;
    uint32_t      m_num_receivers;

    SIppcPortMap  m_ports[2*MAX_NB_OF_CONSUMERS];

} ippc_shmem_sontext_t;

#define PORT_ID_FWD            0x5AU
#define PORT_ID_RECEIVER_1_BCK 0x5BU
#define PORT_ID_RECEIVER_2_BCK 0x5CU
#define PORT_ID_RECEIVER_3_BCK 0x5DU
#define PORT_ID_RECEIVER_4_BCK 0x5EU

#define IPPC_SHEM_PORT_COUNT (MAX_NB_OF_CONSUMERS+1u)
const SIppcPortMap g_ippc_port_configuration[IPPC_SHEM_PORT_COUNT] = {{0, PORT_ID_FWD, PORT_TYPE_BROADCAST},
                                                                    {0, PORT_ID_RECEIVER_1_BCK, PORT_TYPE_UNICAST},
                                                                    {1, PORT_ID_RECEIVER_2_BCK, PORT_TYPE_UNICAST},
                                                                    {2, PORT_ID_RECEIVER_3_BCK, PORT_TYPE_UNICAST},
                                                                    {3, PORT_ID_RECEIVER_4_BCK, PORT_TYPE_UNICAST}};

#endif /* __IPPC_SHEM_COMMON_H */

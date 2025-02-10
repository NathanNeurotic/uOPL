#include <smapregs.h>
#include <thevent.h>
#include <thsemap.h>
#include <stdio.h>
#include <string.h>
#include "ministack.h"
#include "xfer.h"

static uint32_t ip_addr = IP_ADDR(192, 168, 0, 10);
static uint32_t router_addr = IP_ADDR(192, 168, 0, 1);
static int g_arp_event = 0;

#define ARP_RESOLUTION_SUCCESS (1 << 0)
#define ARP_RESOLUTION_TIMEOUT (1 << 1)

typedef struct
{
    uint8_t mac[6];
    uint32_t ip;
} arp_entry_t;
#define MS_ARP_ENTRIES 8
static arp_entry_t arp_table[MS_ARP_ENTRIES];
static int arp_last_overwrite_entry = 0;
static int gSema_ARP_Req = -1;
static int gSema_ARP_Cache = -1;

//private functions
int arp_add_entry(uint32_t ip, uint8_t mac[6]); //local cache
int arp_get_entry(uint32_t ip, uint8_t mac[6]); //local cache
void arp_send_request_for_ip(uint32_t ip);


void eth_packet_init(eth_packet_t *pkt, uint16_t type)
{
#ifdef DEBUG
    //41:42:43:44:45:46 should never go out on the wire!!
    pkt->eth.addr_dst[0] = 0x41;
    pkt->eth.addr_dst[1] = 0x42;
    pkt->eth.addr_dst[2] = 0x43;
    pkt->eth.addr_dst[3] = 0x44;
    pkt->eth.addr_dst[4] = 0x45;
    pkt->eth.addr_dst[5] = 0x46;
#endif
    SMAPGetMACAddress(pkt->eth.addr_src);
    pkt->eth.type = htons(type);
}

void ip_packet_init(ip_packet_t *pkt, uint32_t ip_dest)
{
    eth_packet_init((eth_packet_t *)pkt, ETH_TYPE_IPV4);
    arp_request_entry(ip_dest, pkt->eth.addr_dst);

    // IP, broadcast
    pkt->ip.hlen = 0x45;
    pkt->ip.tos = 0;
    //pkt->ip_len = ;
    pkt->ip.id = 0;
    pkt->ip.flags = 0;
    pkt->ip.frag_offset = 0;
    pkt->ip.ttl = 64;
    pkt->ip.proto = IP_PROTOCOL_UDP;
    //pkt->ip_csum = ;
    pkt->ip.addr_src.addr[0] = (ip_addr >> 24) & 0xff;
    pkt->ip.addr_src.addr[1] = (ip_addr >> 16) & 0xff;
    pkt->ip.addr_src.addr[2] = (ip_addr >> 8) & 0xff;
    pkt->ip.addr_src.addr[3] = (ip_addr)&0xff;
    pkt->ip.addr_dst.addr[0] = (ip_dest >> 24) & 0xff;
    pkt->ip.addr_dst.addr[1] = (ip_dest >> 16) & 0xff;
    pkt->ip.addr_dst.addr[2] = (ip_dest >> 8) & 0xff;
    pkt->ip.addr_dst.addr[3] = (ip_dest)&0xff;
}

void udp_packet_init(udp_packet_t *pkt, uint32_t ip_dst, uint16_t port_dst)
{
    ip_packet_init((ip_packet_t *)pkt, ip_dst);

    //pkt->udp.port_src = ;
    pkt->udp.port_dst = htons(port_dst);
    //pkt->udp.len = ;
    //pkt->udp.csum = ;
}

static uint16_t ip_checksum(ip_header_t *ip)
{
    uint16_t *data = (uint16_t *)ip;
    int count = 10;
    uint32_t csum = 0;

    while (count--)
        csum += *data++;
    csum = (csum >> 16) + (csum & 0xffff);
    csum = (csum >> 16) + (csum & 0xffff);

    return ~((uint16_t)csum & 0xffff);
}

int eth_packet_send_ll(eth_packet_t *pkt, uint16_t pktdatasize, const void *data, uint16_t datasize)
{
    return smap_transmit(pkt, sizeof(eth_header_t) + pktdatasize, data, datasize);
}

int ip_packet_send_ll(ip_packet_t *pkt, uint16_t pktdatasize, const void *data, uint16_t datasize)
{
    pkt->ip.len = htons(sizeof(ip_header_t) + pktdatasize + datasize);
    pkt->ip.csum = 0;
    pkt->ip.csum = ip_checksum(&pkt->ip);

    return eth_packet_send_ll((eth_packet_t *)pkt, sizeof(ip_header_t) + pktdatasize, data, datasize);
}

#define UDP_MAX_PORTS 4
udp_socket_t udp_ports[UDP_MAX_PORTS];
udp_socket_t *udp_bind(uint16_t port_src, udp_port_handler handler, void *handler_arg)
{
    int i;

    for (i = 0; i < UDP_MAX_PORTS; i++) {
        if (udp_ports[i].port_src == 0) {
            udp_ports[i].port_src = port_src;
            udp_ports[i].handler = handler;
            udp_ports[i].handler_arg = handler_arg;
            return &udp_ports[i];
        }
    }

    return NULL;
}

int udp_packet_send_ll(udp_socket_t *socket, udp_packet_t *pkt, uint16_t pktdatasize, const void *data, uint16_t datasize)
{
    pkt->udp.port_src = socket->port_src;
    pkt->udp.len = htons(sizeof(udp_header_t) + pktdatasize + datasize);
    pkt->udp.csum = 0; // not needed

    return ip_packet_send_ll((ip_packet_t *)pkt, sizeof(udp_header_t) + pktdatasize, data, datasize);
}

int arp_add_entry(uint32_t ip, uint8_t mac[6])
{
    int err = -1;
    int i = 0;

    WaitSema(gSema_ARP_Cache);

    // Update existing entry
    for (i = 0; i < MS_ARP_ENTRIES; i++) {
        if (ip == arp_table[i].ip) {
            arp_table[i].mac[0] = mac[0];
            arp_table[i].mac[1] = mac[1];
            arp_table[i].mac[2] = mac[2];
            arp_table[i].mac[3] = mac[3];
            arp_table[i].mac[4] = mac[4];
            arp_table[i].mac[5] = mac[5];
            err = 0;
            goto out;
        }
    }

    {
        arp_last_overwrite_entry = (arp_last_overwrite_entry + 1) % (sizeof(arp_table) / sizeof(*arp_table));
        i = arp_last_overwrite_entry;

        arp_table[i].ip = ip;
        arp_table[i].mac[0] = mac[0];
        arp_table[i].mac[1] = mac[1];
        arp_table[i].mac[2] = mac[2];
        arp_table[i].mac[3] = mac[3];
        arp_table[i].mac[4] = mac[4];
        arp_table[i].mac[5] = mac[5];
        err = 0;
        goto out;
    }

out:
    SignalSema(gSema_ARP_Cache);
    return err;
}

int arp_get_entry(uint32_t ip, uint8_t mac[6])
{
    int err = -1;
    int i;

    WaitSema(gSema_ARP_Cache);
    for (i = 0; i < MS_ARP_ENTRIES; i++) {
        if (ip == arp_table[i].ip) {
            mac[0] = arp_table[i].mac[0];
            mac[1] = arp_table[i].mac[1];
            mac[2] = arp_table[i].mac[2];
            mac[3] = arp_table[i].mac[3];
            mac[4] = arp_table[i].mac[4];
            mac[5] = arp_table[i].mac[5];
            err = 0;
            goto out;
        }
    }
out:
    SignalSema(gSema_ARP_Cache);
    return err;
}

static inline int handle_rx_arp(uint16_t pointer)
{
    USE_SMAP_REGS;
    arp_packet_t req;
    static arp_packet_t reply;
    uint32_t *parp = (uint32_t *)&req;

    SMAP_REG16(SMAP_R_RXFIFO_RD_PTR) = pointer + 12;
    parp[3] = SMAP_REG32(SMAP_R_RXFIFO_DATA);  // 2
    parp[4] = SMAP_REG32(SMAP_R_RXFIFO_DATA);  // 6
    parp[5] = SMAP_REG32(SMAP_R_RXFIFO_DATA);  // 10
    parp[6] = SMAP_REG32(SMAP_R_RXFIFO_DATA);  // 14
    parp[7] = SMAP_REG32(SMAP_R_RXFIFO_DATA);  // 18
    parp[8] = SMAP_REG32(SMAP_R_RXFIFO_DATA);  // 22
    parp[9] = SMAP_REG32(SMAP_R_RXFIFO_DATA);  // 26
    parp[10] = SMAP_REG32(SMAP_R_RXFIFO_DATA); // 30

    if (ntohs(req.arp.oper) == 1 && ntohl(req.arp.target_ip) == ip_addr) {
        reply.eth.addr_dst[0] = req.arp.sender_mac[0];
        reply.eth.addr_dst[1] = req.arp.sender_mac[1];
        reply.eth.addr_dst[2] = req.arp.sender_mac[2];
        reply.eth.addr_dst[3] = req.arp.sender_mac[3];
        reply.eth.addr_dst[4] = req.arp.sender_mac[4];
        reply.eth.addr_dst[5] = req.arp.sender_mac[5];
        SMAPGetMACAddress(reply.eth.addr_src);
        reply.eth.type = htons(ETH_TYPE_ARP);
        reply.arp.htype = htons(1); // ethernet
        reply.arp.ptype = htons(ETH_TYPE_IPV4);
        reply.arp.hlen = 6;
        reply.arp.plen = 4;
        reply.arp.oper = htons(2); // reply
        SMAPGetMACAddress(reply.arp.sender_mac);
        reply.arp.sender_ip = req.arp.target_ip;
        reply.arp.target_mac[0] = req.arp.sender_mac[0];
        reply.arp.target_mac[1] = req.arp.sender_mac[1];
        reply.arp.target_mac[2] = req.arp.sender_mac[2];
        reply.arp.target_mac[3] = req.arp.sender_mac[3];
        reply.arp.target_mac[4] = req.arp.sender_mac[4];
        reply.arp.target_mac[5] = req.arp.sender_mac[5];
        reply.arp.target_ip = req.arp.sender_ip;
        smap_transmit(&reply, 0x2A, NULL, 0);
    } else if (ntohs(req.arp.oper) == 2 && ntohl(req.arp.target_ip) == ip_addr) {
        arp_add_entry(ntohl(req.arp.sender_ip), req.arp.sender_mac);
        SetEventFlag(g_arp_event, ARP_RESOLUTION_SUCCESS);
    }

    return -1;
}

static inline int handle_rx_udp(uint16_t pointer)
{
    USE_SMAP_REGS;
    uint16_t dport;
    int i;

    // Check port
    SMAP_REG16(SMAP_R_RXFIFO_RD_PTR) = pointer + 0x24;
    dport = SMAP_REG16(SMAP_R_RXFIFO_DATA);

    for (i = 0; i < UDP_MAX_PORTS; i++) {
        if (dport == udp_ports[i].port_src)
            return udp_ports[i].handler(&udp_ports[i], pointer, udp_ports[i].handler_arg);
    }

    PRINTF("ministack: udp: dport 0x%X\n", dport);
    return -1;
}

static inline int handle_rx_ipv4(uint16_t pointer)
{
    USE_SMAP_REGS;
    uint8_t protocol;

    // Check ethernet type
    SMAP_REG16(SMAP_R_RXFIFO_RD_PTR) = pointer + 0x14;
    protocol = SMAP_REG32(SMAP_R_RXFIFO_DATA) >> 24;

    switch (protocol) {
        case IP_PROTOCOL_UDP:
            return handle_rx_udp(pointer);
        default:
            PRINTF("ministack: ipv4: protocol 0x%X\n", protocol);
            return -1;
    }
}

int handle_rx_eth(uint16_t pointer)
{
    USE_SMAP_REGS;
    uint16_t eth_type;

    // Check ethernet type
    SMAP_REG16(SMAP_R_RXFIFO_RD_PTR) = pointer + 12;
    eth_type = ntohs(SMAP_REG16(SMAP_R_RXFIFO_DATA));

    switch (eth_type) {
        case ETH_TYPE_ARP:
            return handle_rx_arp(pointer);
        case ETH_TYPE_IPV4:
            return handle_rx_ipv4(pointer);
        default:
            PRINTF("ministack: eth: type 0x%X\n", eth_type);
            return -1;
    }
}

void arp_send_request_for_ip(uint32_t ip)
{
    arp_packet_t pkt;
    eth_packet_init((eth_packet_t *)&pkt.eth, ETH_TYPE_ARP);
    pkt.arp.htype = htons(1); //ethernet
    pkt.arp.ptype = htons(ETH_TYPE_IPV4);
    pkt.arp.hlen = 6;
    pkt.arp.plen = 4;
    pkt.arp.oper = htons(1); // request
    SMAPGetMACAddress(pkt.arp.sender_mac);
    pkt.eth.addr_dst[0] = 0xFF;
    pkt.eth.addr_dst[1] = 0xFF;
    pkt.eth.addr_dst[2] = 0xFF;
    pkt.eth.addr_dst[3] = 0xFF;
    pkt.eth.addr_dst[4] = 0xFF;
    pkt.eth.addr_dst[5] = 0xFF;
    pkt.arp.sender_ip = htonl(ip_addr);
    bzero(pkt.arp.target_mac, sizeof(pkt.arp.target_mac));
    pkt.arp.target_ip = htonl(ip);
    eth_packet_send_ll((eth_packet_t *)&pkt, sizeof(pkt.arp), NULL, 0);
}

static unsigned int _arp_timeout(void *arg)
{
    iSetEventFlag(g_arp_event, ARP_RESOLUTION_TIMEOUT);
    return 0;
}

int arp_request_entry(uint32_t ip, uint8_t mac[6])
{
    //is it already cached maybe?
    if (!arp_get_entry(ip, mac))
        return 0;
    //no arp entry found!

    {
        WaitSema(gSema_ARP_Req);
        //while we were waiting for mutex, was there an entry maybe?
        if (!arp_get_entry(ip, mac)) {
            SignalSema(gSema_ARP_Req);
            return 0;
        }

        iop_sys_clock_t clock;
        arp_send_request_for_ip(ip);

        // Set alarm in case we don't get an ARP response

        /*  FIXME: This timer seems to be much much longer than it should be
            But if i lower it to something reasonable like 200ms, it times out *before* `arp_send_request_for_ip` gets out on the wire
            What is going on here???
        */

        USec2SysClock(5000 * 1000, &clock);
        SetAlarm(&clock, _arp_timeout, NULL);

        // wait for data...
        {
            uint32_t EFBits;
            while (!WaitEventFlag(g_arp_event, ARP_RESOLUTION_TIMEOUT | ARP_RESOLUTION_SUCCESS, WEF_OR | WEF_CLEAR, &EFBits)) {
                if (EFBits & ARP_RESOLUTION_TIMEOUT)
                    break;
                if (!arp_get_entry(ip, mac))
                    break;
            }

            CancelAlarm(_arp_timeout, NULL);
        }

        SignalSema(gSema_ARP_Req);
    }

    //do we have a cache now?
    if (!arp_get_entry(ip, mac))
        return 0;

    if (ip != router_addr) {
        /*  Requested IP wasn't found and we didn't request the router.
            This means we need to route the trafic through the router, so let's do that
        */
        if (!arp_request_entry(router_addr, mac)) {
            //router arp request was successfull, let's cache it for the current IP
            arp_add_entry(ip, mac);
            return 0;
        }
    } else {
        //we failed to resolve mac for router, fallback to broadcast
        uint8_t broadcase_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        arp_add_entry(ip, broadcase_mac);
        arp_get_entry(ip, mac);
    }

    return -1;
}

void ms_ip_set_ip(uint32_t ip)
{
    ip_addr = ip;
}

uint32_t ms_ip_get_ip()
{
    return ip_addr;
}

void ms_router_set_ip(uint32_t ip)
{
    router_addr = ip;
}

uint32_t ms_router_get_ip()
{
    return router_addr;
}

int ministack_init()
{
    iop_event_t EventFlagData;
    EventFlagData.attr = 0;
    EventFlagData.option = 0;
    EventFlagData.bits = 0;
    g_arp_event = CreateEventFlag(&EventFlagData);

    gSema_ARP_Req = CreateMutex(IOP_MUTEX_UNLOCKED);
    gSema_ARP_Cache = CreateMutex(IOP_MUTEX_UNLOCKED);

    bzero(arp_table, sizeof(arp_table));
    return 0;
}
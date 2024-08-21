/*
 * io.h: I/O thread definitions
 */

#pragma once

#include <skyloft/net.h>
#include <skyloft/params.h>
#include <skyloft/sched.h>
#include <utils/defs.h>
#include <utils/shm.h>

/* preamble to ingress network packets */
struct rx_net_hdr {
    uint64_t completion_data; /* a tag to help complete the request */
    uint32_t len;             /* the length of the payload */
    uint32_t rss_hash;        /* the HW RSS 5-tuple hash */
    uint32_t csum_type;       /* the type of checksum */
    uint32_t csum;            /* 16-bit one's complement */
    char payload[];           /* packet data */
};

/* preamble to egress network packets */
struct tx_net_hdr {
    uint64_t completion_data; /* a tag to help complete the request */
    uint32_t len;             /* the length of the payload */
    uint32_t olflags;         /* offload flags */
    uint16_t pad;             /* because of 14 byte ethernet header */
    uint8_t payload[];        /* packet data */
} __attribute__((__packed__));

/* possible values for @csum_type above */
enum {
    /*
     * Hardware did not provide checksum information.
     */
    CHECKSUM_TYPE_NEEDED = 0,

    /*
     * The checksum was verified by hardware and found to be valid.
     */
    CHECKSUM_TYPE_UNNECESSARY,

    /*
     * Hardware provided a 16 bit one's complement sum from after the LL
     * header to the end of the packet. VLAN tags (if present) are included
     * in the sum. This is the most robust checksum type because it's useful
     * even if the NIC can't parse the headers.
     */
    CHECKSUM_TYPE_COMPLETE,
};

/* possible values for @olflags above */
#define OLFLAG_IP_CHKSUM  BIT(0) /* enable IP checksum generation */
#define OLFLAG_TCP_CHKSUM BIT(1) /* enable TCP checksum generation */
#define OLFLAG_IPV4       BIT(2) /* indicates the packet is IPv4 */
#define OLFLAG_IPV6       BIT(3) /* indicates the packet is IPv6 */

/*
 * RX queues: IOKERNEL -> RUNTIMES
 * These queues multiplex several different types of requests.
 */
enum {
    RX_NET_RECV = 0, /* points to a struct rx_net_hdr */
    RX_NET_COMPLETE, /* contains tx_net_hdr.completion_data */
    RX_CALL_NR,      /* number of commands */
};

/*
 * TX packet queues: RUNTIMES -> IOKERNEL
 * These queues are only for network packets and can experience HOL blocking.
 */
enum {
    TXPKT_NET_XMIT = 0, /* points to a struct tx_net_hdr */
    TXPKT_NR,           /* number of commands */
};

/*
 * TX command queues: RUNTIMES -> IOKERNEL
 * These queues handle a variety of commands, and typically they are handled
 * much faster by the IOKERNEL than packets, so no HOL blocking.
 */
enum {
    TXCMD_NET_COMPLETE = 0, /* contains rx_net_hdr.completion_data */
    TXCMD_NR,               /* number of commands */
};

/*
 * Helpers
 */

int str_to_ip(const char *str, uint32_t *addr);
int str_to_mac(const char *str, struct eth_addr *addr);

/*
 * I/O thread
 */

struct iothread_t {
    /* dpdk port id */
    int port_id;
    /* dpdk rx memory pool */
    struct rte_mempool *rx_mbuf_pool;
    /* dpdk tx memory pool */
    struct rte_mempool *tx_mbuf_pool;
    /* communication shared memory */
    struct shm_region tx_region;
    /* network configurations */
    uint32_t addr;
    uint32_t netmask;
    uint32_t gateway;
    struct eth_addr mac;
};

extern struct iothread_t *io;
extern physaddr_t* page_paddrs;

int iothread_init(void);
__noreturn void iothread_main(void);

int dpdk_init(void);
void dpdk_print_eth_stats(void);
int dpdk_late_init(void);

int rx_init(void);
bool rx_burst(void);
bool rx_send_to_runtime(struct proc *p, uint32_t hash, uint64_t cmd, uint64_t payload);

int tx_init(void);
bool tx_burst(void);
int tx_drain_completions(struct proc *p, int n);
bool tx_send_completion(void *obj);

bool cmd_rx_burst(void);

/* the egress buffer pool must be large enough to fill all the TXQs entirely */
#define EGRESS_POOL_SIZE(nks) (IO_PKTQ_SIZE * MBUF_DEFAULT_LEN * MAX(16, (nks)) * 16UL)
/*-
 *   BSD LICENSE
 *
 *   Copyright(c) 2010-2015 Intel Corporation. All rights reserved.
 *   All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of Intel Corporation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * dpdk.c - DPDK initialization for the iokernel dataplane
 */

#include <rte_arp.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_ether.h>
#include <rte_lcore.h>

#include <net/ethernet.h>
#include <rte_mempool.h>
#include <skyloft/io.h>
#include <skyloft/params.h>
#include <skyloft/platform.h>
#include <utils/log.h>
#include <utils/time.h>

#define DEFAULT_RXQ_SIZE 128 /* default rx queue size  */
#define DEFAULT_TXQ_SIZE 128 /* default tx queue size  */

static const struct rte_eth_conf port_conf_default = {
    .rxmode =
        {
            .max_lro_pkt_size = ETH_MAX_LEN,
            .offloads = RTE_ETH_RX_OFFLOAD_IPV4_CKSUM,
            .mq_mode = RTE_ETH_MQ_RX_RSS,
        },
    .rx_adv_conf =
        {
            .rss_conf =
                {
                    .rss_key = NULL,
                    .rss_hf = RTE_ETH_RSS_NONFRAG_IPV4_TCP | RTE_ETH_RSS_NONFRAG_IPV4_UDP,
                },
        },
    .txmode =
        {
            .offloads = RTE_ETH_TX_OFFLOAD_IPV4_CKSUM | RTE_ETH_TX_OFFLOAD_UDP_CKSUM |
                        RTE_ETH_TX_OFFLOAD_TCP_CKSUM,
        },
};

/*
 * Initializes a given port using global settings and with the RX buffers
 * coming from the mbuf_pool passed as a parameter.
 */
static inline int dpdk_port_init(uint8_t port, struct rte_mempool *mbuf_pool)
{
    struct rte_eth_conf port_conf = port_conf_default;
    const uint16_t rx_rings = 1, tx_rings = 1;
    uint16_t nb_rxd = DEFAULT_RXQ_SIZE;
    uint16_t nb_txd = DEFAULT_TXQ_SIZE;
    int retval;
    uint16_t q;
    struct rte_eth_dev_info dev_info;
    struct rte_eth_txconf *txconf;
    struct rte_eth_rxconf *rxconf;

    if (!rte_eth_dev_is_valid_port(port))
        return -1;

    /* Configure the Ethernet device. */
    retval = rte_eth_dev_configure(port, rx_rings, tx_rings, &port_conf);
    if (retval != 0)
        return retval;

    retval = rte_eth_dev_adjust_nb_rx_tx_desc(port, &nb_rxd, &nb_txd);
    if (retval != 0)
        return retval;

    rte_eth_dev_info_get(0, &dev_info);
    rxconf = &dev_info.default_rxconf;
    rxconf->rx_free_thresh = 64;

    /* Allocate and set up 1 RX queue per Ethernet port. */
    for (q = 0; q < rx_rings; q++) {
        retval =
            rte_eth_rx_queue_setup(port, q, nb_rxd, rte_eth_dev_socket_id(port), rxconf, mbuf_pool);
        if (retval < 0)
            return retval;
    }

    /* Enable TX offloading */
    txconf = &dev_info.default_txconf;
    txconf->tx_rs_thresh = 64;
    txconf->tx_free_thresh = 64;

    /* Allocate and set up 1 TX queue per Ethernet port. */
    for (q = 0; q < tx_rings; q++) {
        retval = rte_eth_tx_queue_setup(port, q, nb_txd, rte_eth_dev_socket_id(port), txconf);
        if (retval < 0)
            return retval;
    }

    /* Start the Ethernet port. */
    retval = rte_eth_dev_start(port);
    if (retval < 0)
        return retval;

    /* Display the port MAC address. */
    struct rte_ether_addr addr;
    rte_eth_macaddr_get(port, &addr);
    log_info("dpdk: port %u MAC %02X:%02X:%02X:%02X:%02X:%02X", (unsigned)port, addr.addr_bytes[0],
             addr.addr_bytes[1], addr.addr_bytes[2], addr.addr_bytes[3], addr.addr_bytes[4],
             addr.addr_bytes[5]);

    /* Enable RX in promiscuous mode for the Ethernet device. */
    rte_eth_promiscuous_enable(port);

    return 0;
}

/*
 * Log some ethernet port stats.
 */
void dpdk_print_eth_stats()
{
    int ret;
    struct rte_eth_stats stats;

    ret = rte_eth_stats_get(io->port_id, &stats);
    if (ret)
        log_debug("dpdk: error getting eth stats");

    fprintf(stderr, "eth stats for port %d at time %" PRIu64 "\n", io->port_id, now_us());
    fprintf(stderr, "RX-packets: %" PRIu64 " RX-dropped: %" PRIu64 " RX-bytes: %" PRIu64 "\n",
            stats.ipackets, stats.imissed, stats.ibytes);
    fprintf(stderr, "TX-packets: %" PRIu64 " TX-bytes: %" PRIu64 "\n", stats.opackets,
            stats.obytes);
    fprintf(stderr, "RX-error: %" PRIu64 " TX-error: %" PRIu64 " RX-mbuf-fail: %" PRIu64 "\n",
            stats.ierrors, stats.oerrors, stats.rx_nombuf);
}

/*
 * Initialize dpdk, must be done as soon as possible.
 */
int dpdk_init()
{
    char *argv[4];
    char buf[10];

    /* init args */
    argv[0] = "./skyloft_iothread";
    argv[1] = "-l";
    /* use our assigned core */
    sprintf(buf, "%d", hw_cpu_id(IO_CPU));
    argv[2] = buf;
    argv[3] = "--socket-mem=128";

    /* initialize the Environment Abstraction Layer (EAL) */
    int ret = rte_eal_init(sizeof(argv) / sizeof(argv[0]), argv);
    if (ret < 0) {
        log_err("dpdk: error with EAL initialization");
        return -EINVAL;
    }

    /* check that there is a port to send/receive on */
    if (!rte_eth_dev_is_valid_port(0)) {
        log_err("dpdk: no available ports");
        return -EBUSY;
    }

    if (rte_lcore_count() > 1)
        log_warn("dpdk: too many lcores enabled, only 1 used");

    return 0;
}

/*
 * Additional dpdk initialization that must be done after rx init.
 */
int dpdk_late_init()
{
    /* initialize port */
    io->port_id = 0;
    if (dpdk_port_init(io->port_id, io->rx_mbuf_pool) != 0) {
        log_err("dpdk: cannot init port %" PRIu8 "\n", io->port_id);
        return -1;
    }

    if (rte_eth_dev_socket_id(io->port_id) != (int)rte_socket_id()) {
        log_err("dpdk: port %u (socket %d) is on remote NUMA node to polling thread (socket %d). "
                "Performance will not be optimal.",
                io->port_id, rte_eth_dev_socket_id(io->port_id), rte_socket_id());
    }

    log_info("dpdk: I/O thread on CPU %u", rte_lcore_id());

    return 0;
}

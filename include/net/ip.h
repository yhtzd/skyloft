#pragma once

#include <netinet/in.h>
#include <netinet/ip.h>

#include <utils/byteorder.h>
#include <utils/types.h>

#define MAKE_IP_ADDR(a, b, c, d) \
    (((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)c << 8) | (uint32_t)d)

#define IP_ADDR_STR_LEN 16

/*
 * Structure of an internet header, naked of options.
 */
struct ip_hdr {
#if __BYTE_ORDER == __LITTLE_ENDIAN
    uint8_t header_len : 4; /* header length */
    uint8_t version : 4;    /* version */
#endif
#if __BYTE_ORDER == __BIG_ENDIAN
    uint8_t version : 4;    /* version */
    uint8_t header_len : 4; /* header length */
#endif
    uint8_t tos;          /* type of service */
    uint16_t len;         /* total length */
    uint16_t id;          /* identification */
    uint16_t off;         /* fragment offset field */
#define IP_RF      0x8000 /* reserved fragment flag */
#define IP_DF      0x4000 /* dont fragment flag */
#define IP_MF      0x2000 /* more fragments flag */
#define IP_OFFMASK 0x1fff /* mask for fragmenting bits */
    uint8_t ttl;          /* time to live */
    uint8_t proto;        /* protocol */
    uint16_t cksum;      /* checksum */
    uint32_t saddr;       /* source address */
    uint32_t daddr;       /* dest address */
} __packed __aligned(4);

/*
 * Definitions for DiffServ Codepoints as per RFC2474
 */
#define	IPTOS_DSCP_CS0		0x00
#define	IPTOS_DSCP_CS1		0x20
#define	IPTOS_DSCP_AF11		0x28
#define	IPTOS_DSCP_AF12		0x30
#define	IPTOS_DSCP_AF13		0x38
#define	IPTOS_DSCP_CS2		0x40
#define	IPTOS_DSCP_AF21		0x48
#define	IPTOS_DSCP_AF22		0x50
#define	IPTOS_DSCP_AF23		0x58
#define	IPTOS_DSCP_CS3		0x60
#define	IPTOS_DSCP_AF31		0x68
#define	IPTOS_DSCP_AF32		0x70
#define	IPTOS_DSCP_AF33		0x78
#define	IPTOS_DSCP_CS4		0x80
#define	IPTOS_DSCP_AF41		0x88
#define	IPTOS_DSCP_AF42		0x90
#define	IPTOS_DSCP_AF43		0x98
#define	IPTOS_DSCP_CS5		0xa0
#define	IPTOS_DSCP_EF		0xb8
#define	IPTOS_DSCP_CS6		0xc0
#define	IPTOS_DSCP_CS7		0xe0

/*
 * ECN (Explicit Congestion Notification) codepoints in RFC3168 mapped to the
 * lower 2 bits of the TOS field.
 */
#define	IPTOS_ECN_NOTECT	0x00	/* not-ECT */
#define	IPTOS_ECN_ECT1		0x01	/* ECN-capable transport (1) */
#define	IPTOS_ECN_ECT0		0x02	/* ECN-capable transport (0) */
#define	IPTOS_ECN_CE		0x03	/* congestion experienced */
#define	IPTOS_ECN_MASK		0x03	/* ECN field mask */

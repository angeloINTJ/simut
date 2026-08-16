#pragma once

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

// Critical section protection
extern void noInterrupts();
extern void interrupts();
#define SYS_ARCH_DECL_PROTECT int
#define SYS_ARCH_PROTECT(lev) {(void) lev; noInterrupts();}
#define SYS_ARCH_UNPROTECT(lev) {(void) lev; interrupts();}

#ifndef DEBUG_RP2040_PORT
extern void panic(const char *fmt, ...);
#define LWIP_PLATFORM_ASSERT(x) panic("lwip")
#endif

extern unsigned long __lwip_rand(void);
#define LWIP_RAND() __lwip_rand()

#ifndef __LWIP_MEMMULT
#define __LWIP_MEMMULT 1
#endif

// Common settings used in most of the pico_w examples
// (see https://www.nongnu.org/lwip/2_1_x/group__lwip__opts.html for details)

#define NO_SYS                        1
#define LWIP_SOCKET                   0
#define MEM_LIBC_MALLOC               0

#define MEM_ALIGNMENT                 4
#define MEM_SIZE                      (__LWIP_MEMMULT * 16384)
#define MEMP_NUM_TCP_SEG              (32)
#define MEMP_NUM_ARP_QUEUE            (10)
/* SIMUT: de volta ao default 24, revertendo o corte para 12.
 *
 * A justificativa do corte era "1-2 conexões TCP simultâneas no device típico
 * — não satura mesmo com 12 envelopes". Medido em 2026-08-02, satura: contra
 * um servidor que responde 1 MB, o pool ia a 12/12 e o servidor web ficava
 * mudo, sem recuperar. Parte disso era um vazamento de _rx_buf no
 * ClientContext (corrigido em patches/clientcontext_rx_leak.patch, que sozinho
 * levou o aparelho de ~16 para ~144 conexões antes de saturar).
 *
 * RETRATAÇÃO (2026-08-10, D14): este comentário dizia "resta uma segunda fonte
 * não localizada". NÃO resta — não há vazamento nenhum. Ninguém achava a
 * segunda fonte porque não sobrara uma primeira. O que se media era o PICO do
 * pool (marca d'água, que por definição nunca desce) e a contagem de falhas; o
 * número que separa vazamento de pressão é o "em uso depois que a carga para",
 * e ele volta ao basal em todos os níveis de concorrência — inclusive no que
 * esgotou o pool e falhou 79 alocações. A causa era aritmética, e está no
 * TCP_WND logo abaixo.
 *
 * Custo: ~18 KB de BSS. Com o firmware em ~30% de RAM, cabe.
 * Aumentar o pool seria a saída errada: 24 entradas já são 35,5 KB de BSS e
 * dobrar custa mais que o heap livre inteiro.
 * UDP/BT seguem em default (reduzir UDP_PCB quebra mDNS; mexer em BT quebra o
 * RSSI via chip cyw43 compartilhado). */
#define PBUF_POOL_SIZE                (__LWIP_MEMMULT > 1 ? 32 : 24)
#define LWIP_ARP                      7
#define LWIP_ETHERNET                 1
#define LWIP_ICMP                     1
#define LWIP_RAW                      1
/* SIMUT: receive window halved, 8 -> 4 MSS, to stop the device advertising
 * more buffering than the pool can back.
 *
 * PBUF_POOL entries are ~1514 B each, so an 8*MSS window is 7,7 entries a
 * connection. Six connections filling their windows want 46 against a pool of
 * 24, and measured on the bench 2026-08-10 that is exactly what happens: four
 * concurrent clients peak at 13 entries and never fail, five reach 24/24 with
 * 45 failed allocations, six with 79. Nothing leaks — `em uso` returns to
 * baseline every time — the pool is simply promised out twice over.
 *
 * The window costs nothing to give up because the device cannot use it. Uploads
 * run at 26 KB/s, bound by flash writes; at a ~5 ms RTT even 4*MSS allows about
 * 1,1 MB/s, forty times more than the device can absorb. Downloads are governed
 * by TCP_SND_BUF, not this, so they are untouched.
 *
 * 4*MSS puts six connections at 23 entries — inside the pool with the peak
 * still bounded by it rather than by the sum of the windows. */
#define TCP_WND                       (4 * TCP_MSS)
#define TCP_MSS                       1460
#define TCP_SND_BUF                   (8 * TCP_MSS)
#define TCP_SND_QUEUELEN              ((4 * (TCP_SND_BUF) + (TCP_MSS - 1)) / (TCP_MSS))
#define TCP_LISTEN_BACKLOG            1
#define TCP_DEFAULT_LISTEN_BACKLOG    (2 * __LWIP_MEMMULT)
#define LWIP_NETIF_STATUS_CALLBACK    1
#define LWIP_NETIF_LINK_CALLBACK      1
#define LWIP_NETIF_HOSTNAME           1
#define LWIP_NUM_NETIF_CLIENT_DATA    5
#define LWIP_NETCONN                  0
#define LWIP_CHKSUM_ALGORITHM         0
#define LWIP_DHCP                     1
#define LWIP_IPV4                     1
#define LWIP_TCP                      1
#define LWIP_UDP                      1
#define LWIP_DNS                      1
#define LWIP_DNS_SUPPORT_MDNS_QUERIES 1
#define LWIP_TCP_KEEPALIVE            1
#define LWIP_NETIF_TX_SINGLE_PBUF     1
#define DHCP_DOES_ARP_CHECK           0
#define LWIP_DHCP_DOES_ACD_CHECK      0
#define LWIP_IGMP                     1
#define LWIP_MDNS_RESPONDER           1
#define MDNS_MAX_SERVICES             4

// See #1285
#define MEMP_NUM_UDP_PCB              (__LWIP_MEMMULT * 7)
#define MEMP_NUM_TCP_PCB              (__LWIP_MEMMULT * 5)

#if LWIP_IPV6
#define LWIP_IPV6_DHCP6               1
#define LWIP_IPV6_MLD                 1
#endif

// NTP
extern void __setSystemTime(unsigned long long sec, unsigned long us);
#define SNTP_SET_SYSTEM_TIME_US(sec, us)  __setSystemTime(sec, us)
#define SNTP_MAX_SERVERS                  2
//#define SNTP_SERVER_ADDRESS               "pool.ntp.org"
#define SNTP_SERVER_DNS                   1

#ifndef LWIP_DEBUG
/* T0.3 (plano de estabilidade): MEMP/PBUF counters SEMPRE ligados —
 * footprint de ~300 B, zero prints. `show net status` expõe o pico de
 * uso e as falhas de alocacao do PBUF pool para decidir T2.2 (12→16). */
#define LWIP_STATS                    1
#define LWIP_STATS_DISPLAY            0
#define MEM_STATS                     0
#define SYS_STATS                     0
#define MEMP_STATS                    1
#define LINK_STATS                    0
#else
#define LWIP_STATS                    1
#define LWIP_STATS_DISPLAY            1
#define MEM_STATS                     1
#define SYS_STATS                     1
#define MEMP_STATS                    1
#define LINK_STATS                    1
#define ETHARP_DEBUG                  LWIP_DBG_ON
#define NETIF_DEBUG                   LWIP_DBG_ON
#define PBUF_DEBUG                    LWIP_DBG_ON
#define API_LIB_DEBUG                 LWIP_DBG_ON
#define API_MSG_DEBUG                 LWIP_DBG_ON
#define SOCKETS_DEBUG                 LWIP_DBG_ON
#define ICMP_DEBUG                    LWIP_DBG_ON
#define INET_DEBUG                    LWIP_DBG_ON
#define IP_DEBUG                      LWIP_DBG_ON
#define IP_REASS_DEBUG                LWIP_DBG_ON
#define RAW_DEBUG                     LWIP_DBG_ON
#define MEM_DEBUG                     LWIP_DBG_ON
#define MEMP_DEBUG                    LWIP_DBG_ON
#define SYS_DEBUG                     LWIP_DBG_ON
#define TCP_DEBUG                     LWIP_DBG_ON
#define TCP_INPUT_DEBUG               LWIP_DBG_ON
#define TCP_OUTPUT_DEBUG              LWIP_DBG_ON
#define TCP_RTO_DEBUG                 LWIP_DBG_ON
#define TCP_CWND_DEBUG                LWIP_DBG_ON
#define TCP_WND_DEBUG                 LWIP_DBG_ON
#define TCP_FR_DEBUG                  LWIP_DBG_ON
#define TCP_QLEN_DEBUG                LWIP_DBG_ON
#define TCP_RST_DEBUG                 LWIP_DBG_ON
#define UDP_DEBUG                     LWIP_DBG_ON
#define TCPIP_DEBUG                   LWIP_DBG_ON
#define PPP_DEBUG                     LWIP_DBG_ON
#define SLIP_DEBUG                    LWIP_DBG_ON
#define DHCP_DEBUG                    LWIP_DBG_ON
#endif


#ifdef __cplusplus
}
#endif // __cplusplus

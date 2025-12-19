#ifndef APP_INIT_H
#define APP_INIT_H

#include <stdint.h>
#include <stdbool.h>
#include <rte_mempool.h>
#include <rte_ring.h>

#ifdef __cplusplus
extern "C" {
#endif

extern uint16_t phy_port_id;
extern uint16_t virt_port_id;
extern struct rte_ring *hft_ring;
extern volatile bool force_quit;

void init_port_mapping(void);
void configure_ports(struct rte_mempool *mbuf_pool);
void close_ports(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_INIT_H */

bin_PROGRAMS = client_acl\
               client_bridging\
               client_cfg_purge\
               client_debugcomp\
               client_debuglvl\
               client_event\
               client_flowtable_dump\
               client_group\
               client_ingress_port\
               client_pkt_txrx\
               client_port\
               client_routing\
               client_srcmac_learn\
               client_termmac\
               client_tunnel_ecmp_nexthop\
               client_tunnel_ecmp_nexthop_member\
               client_tunnel_nexthop\
               client_tunnel_port\
               client_tunnel_port_tenant\
               client_tunnel_tenant\
               client_vlan

client_acl_SOURCES = client_acl.c
client_bridging_SOURCES = client_bridging.c
client_cfg_purge_SOURCES = client_cfg_purge.c
client_debugcomp_SOURCES = client_debugcomp.c
client_debuglvl_SOURCES = client_debuglvl.c
client_event_SOURCES = client_event.c
client_flowtable_dump_SOURCES = client_flowtable_dump.c
client_group_SOURCES = client_group.c
client_ingress_port_SOURCES = client_ingress_port.c
client_pkt_txrx_SOURCES = client_pkt_txrx.c
client_port_SOURCES = client_port.c
client_routing_SOURCES = client_routing.c
client_srcmac_learn_SOURCES = client_srcmac_learn.c
client_termmac_SOURCES = client_termmac.c
client_tunnel_ecmp_nexthop_SOURCES = client_tunnel_ecmp_nexthop.c
client_tunnel_ecmp_nexthop_member_SOURCES = client_tunnel_ecmp_nexthop_member.c
client_tunnel_nexthop_SOURCES = client_tunnel_nexthop.c
client_tunnel_port_SOURCES = client_tunnel_port.c
client_tunnel_port_tenant_SOURCES = client_tunnel_port_tenant.c
client_tunnel_tenant_SOURCES = client_tunnel_tenant.c
client_vlan_SOURCES = client_vlan.c

AM_CFLAGS = -I$(top_srcdir)/src/include -Wl,-Bstatic -Wl,-Bdynamic
AM_LDFLAGS = -L$(top_srcdir)/bin/gto-trident-brl20
LDADD = -lc -lrpc_client

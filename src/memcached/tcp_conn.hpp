#ifndef MEMCACHED_TCP_CONN_HPP_
#define MEMCACHED_TCP_CONN_HPP_

#include "arch/io/network.hpp"
#include "arch/types.hpp"
#include "concurrency/auto_drainer.hpp"
#include "memcached/protocol.hpp"
#include "memcached/clustered_store.hpp"

/* Serves memcache queries over the given TCP connection until the connection in question
is closed. */

void serve_memcache(tcp_conn_t *conn, get_store_t *get_store, set_store_interface_t *set_store);

/* Listens for TCP connections on the given port and serves memcache queries over those
connections until the destructor is called. */

struct memcache_listener_t : public home_thread_mixin_t {

    memcache_listener_t(int port, namespace_interface_t<memcached_protocol_t> *namespace_if);

private:
    cluster_get_store_t get_store;
    cluster_set_store_t set_store;
    timestamping_set_store_interface_t ts_set_store;

    int next_thread;

    /* We use this to make sure that all TCP connections stop when the
    `memcached_listener_t` is destroyed. */
    auto_drainer_t drainer;

    tcp_listener_t tcp_listener;

    void handle(auto_drainer_t::lock_t keepalive, boost::scoped_ptr<nascent_tcp_conn_t>& conn);
};

#endif /* MEMCACHED_TCP_CONN_HPP_ */

/**
 * libnetl-private.h - Internal socket interfaces.
 * Author: KaisarCode
 */
#ifndef KC_NETL_PRIVATE_H
#define KC_NETL_PRIVATE_H
#include "libnetl.h"
typedef struct kc_netl_socket kc_netl_socket_t;
typedef struct {
    const char *host;
    unsigned short port;
    int protocol;
    const char *local_host;
    unsigned short local_port;
} kc_netl_connect_options_t;
int kc_netl_socket_open(kc_netl_socket_t **out,const kc_netl_options_t *options,kc_netl_receive_fn receive,kc_netl_disconnect_fn disconnect,kc_netl_error_fn error,void *userdata);
int kc_netl_socket_connect(kc_netl_socket_t **out,kc_netl_peer_t **peer,const kc_netl_connect_options_t *options,kc_netl_receive_fn receive,kc_netl_disconnect_fn disconnect,kc_netl_error_fn error,void *userdata);
unsigned short kc_netl_socket_port(const kc_netl_socket_t *socket);
void kc_netl_socket_close(kc_netl_socket_t *socket);
#endif

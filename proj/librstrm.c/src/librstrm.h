/**
 * librstrm.h - Reliable ordered byte stream.
 * Summary: Reliable stream over caller-supplied datagram transport.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

#ifndef KC_RSTRM_H
#define KC_RSTRM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct kc_rstrm kc_rstrm_t;

#define KC_RSTRM_OK          0
#define KC_RSTRM_EINVAL     -1
#define KC_RSTRM_ECLOSED    -2
#define KC_RSTRM_EFULL      -3
#define KC_RSTRM_ETRANSPORT -4
#define KC_RSTRM_EPROTO     -5
#define KC_RSTRM_ENOMEM     -6

typedef struct {
    size_t mtu;
} kc_rstrm_options_t;

/**
 * Receives ordered reliable stream bytes.
 *
 * Callback boundaries do not preserve send boundaries. The bytes are borrowed
 * and remain valid only for the callback duration.
 *
 * @param data Ordered stream bytes.
 * @param data_size Byte count.
 * @param userdata Consumer data supplied when opening the stream.
 * @return None.
 */
typedef void (*kc_rstrm_receive_fn)(
    const void *data,
    size_t data_size,
    void *userdata
);

/**
 * Emits one complete datagram for the caller-supplied transport.
 *
 * The bytes are borrowed and remain valid only for the callback duration. The
 * callback must consume or copy the complete datagram before returning.
 *
 * @param data Datagram bytes.
 * @param data_size Datagram byte count.
 * @param userdata Consumer data supplied when opening the stream.
 * @return Zero when accepted by the transport, nonzero on transport failure.
 */
typedef int (*kc_rstrm_datagram_fn)(
    const void *data,
    size_t data_size,
    void *userdata
);

/**
 * Receives notification that the peer gracefully ended its write side.
 * All peer bytes sent before shutdown are delivered before this callback.
 *
 * @param userdata Consumer data supplied when opening the stream.
 * @return None.
 */
typedef void (*kc_rstrm_eof_fn)(void *userdata);

/**
 * Receives notification that send capacity returned after KC_RSTRM_EFULL.
 *
 * @param userdata Consumer data supplied when opening the stream.
 * @return None.
 */
typedef void (*kc_rstrm_drain_fn)(void *userdata);

/**
 * Receives one asynchronous terminal stream failure.
 * No further receive, eof, or drain callbacks are delivered afterward.
 *
 * @param status Public failure status.
 * @param userdata Consumer data supplied when opening the stream.
 * @return None.
 */
typedef void (*kc_rstrm_error_fn)(int status, void *userdata);

/**
 * Opens one reliable full-duplex byte stream over an external datagram channel.
 *
 * Both endpoints use the same wire protocol and have no client/server role.
 * The datagram channel must be exclusive to one rstrm session and preserve
 * datagram boundaries. It may lose, duplicate, delay, or reorder datagrams.
 *
 * options may be NULL for defaults. mtu zero selects the default. mtu is the
 * maximum complete datagram size emitted through datagram. receive and datagram
 * are required; eof, drain, and error are optional.
 *
 * The library owns protocol scheduling internally. Callers only pass incoming
 * datagrams to kc_rstrm_input() and move emitted datagrams through their chosen
 * transport.
 *
 * @param out Destination stream pointer.
 * @param options Optional stream options.
 * @param receive Ordered stream receive callback.
 * @param datagram Datagram output callback.
 * @param eof Optional remote graceful-shutdown callback.
 * @param drain Optional send-capacity callback.
 * @param error Optional asynchronous terminal error callback.
 * @param userdata Consumer data passed to callbacks.
 * @return KC_RSTRM_OK on success, otherwise a negative status.
 */
int kc_rstrm_open(
    kc_rstrm_t **out,
    const kc_rstrm_options_t *options,
    kc_rstrm_receive_fn receive,
    kc_rstrm_datagram_fn datagram,
    kc_rstrm_eof_fn eof,
    kc_rstrm_drain_fn drain,
    kc_rstrm_error_fn error,
    void *userdata
);

/**
 * Queues bytes for reliable ordered delivery.
 *
 * The library copies the supplied bytes before returning. Send boundaries have
 * no meaning to the receiving stream. KC_RSTRM_EFULL is non-terminal and means
 * the caller should pause producing bytes until drain is called.
 *
 * @param rstrm Open stream.
 * @param data Bytes to send.
 * @param data_size Byte count.
 * @return KC_RSTRM_OK, KC_RSTRM_EFULL, KC_RSTRM_ECLOSED, or another failure.
 */
int kc_rstrm_send(
    kc_rstrm_t *rstrm,
    const void *data,
    size_t data_size
);

/**
 * Supplies one complete datagram received from the underlying transport.
 *
 * The library copies the datagram before returning. Datagram boundaries must be
 * preserved: one transport datagram corresponds to one call.
 *
 * KC_RSTRM_EFULL means the local input queue is saturated; dropping that
 * datagram is safe because reliability will cause retransmission.
 *
 * @param rstrm Open stream.
 * @param data One complete datagram.
 * @param data_size Datagram byte count.
 * @return KC_RSTRM_OK, KC_RSTRM_EFULL, KC_RSTRM_ECLOSED, or another failure.
 */
int kc_rstrm_input(
    kc_rstrm_t *rstrm,
    const void *data,
    size_t data_size
);

/**
 * Gracefully ends the local write side after all previously queued bytes.
 *
 * The stream remains able to receive peer bytes and eof. Repeated shutdown is
 * harmless. Sends after shutdown return KC_RSTRM_ECLOSED.
 *
 * @param rstrm Open stream.
 * @return KC_RSTRM_OK or KC_RSTRM_ECLOSED.
 */
int kc_rstrm_shutdown(kc_rstrm_t *rstrm);

/**
 * Closes and releases one stream locally.
 *
 * Close is abortive: it does not wait for queued bytes or a graceful shutdown
 * to reach the peer. Call shutdown first when graceful write completion matters.
 * NULL is accepted. Close may be called from a callback; release is deferred
 * until that callback returns. No callback is delivered after an external close
 * call returns.
 *
 * @param rstrm Stream, or NULL.
 * @return None.
 */
void kc_rstrm_close(kc_rstrm_t *rstrm);

/**
 * Returns a static message for one public status code.
 * @param status Public status code.
 * @return Static status message.
 */
const char *kc_rstrm_strerror(int status);

/**
 * Returns the build version generated at compile time.
 * @return Build version.
 */
uint64_t kc_rstrm_version(void);

#ifdef __cplusplus
}
#endif

#endif

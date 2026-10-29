# TLS security and sessions

## Client authentication

Client mode requires `host`, which is used for SNI and hostname verification.

When `ca` is omitted, peer trust uses the native platform trust roots:

- Linux and Android load the system certificate store into Mbed TLS.
- Windows validates the peer chain with CryptoAPI.
- macOS and iOS validate the peer chain with Security.framework.

`ca` and `ca_size` may instead provide a PEM or DER trust chain.

## Server sessions

Server mode requires a certificate chain and private key:

```c
kc_tls_options_t options = {
    .role = KC_TLS_SERVER,
    .certificate = certificate_bytes,
    .certificate_size = certificate_size,
    .private_key = private_key_bytes,
    .private_key_size = private_key_size
};
```

PEM and DER inputs are accepted. Server mode currently authenticates the server
to the client and does not require a client certificate.

## TLS backend

TLS protocol and cryptography are provided by Mbed TLS 3.6.7 vendored under
`lib/mbedtls` and compiled into the library. OpenSSL and a system Mbed TLS
installation are not required.

TLS 1.2 is the minimum protocol version. TLS 1.3 is enabled by the embedded
backend.

## Next step

Return to the [project README](../README.md).

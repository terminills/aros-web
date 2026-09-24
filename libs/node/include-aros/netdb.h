/*
 * AROSTCP exposes the modern addrinfo structures and constants without the
 * corresponding POSIX function prototypes.  OpenSSL uses AI_PASSIVE as its
 * feature test, so hide it only while building OpenSSL and select OpenSSL's
 * gethostbyname/getservbyname compatibility implementation.
 */
#ifndef AROS_NODE_OPENSSL_NETDB_H
#define AROS_NODE_OPENSSL_NETDB_H

#include_next <netdb.h>

#ifdef OPENSSL_BUILDING_OPENSSL
#undef AI_PASSIVE
#endif

#endif

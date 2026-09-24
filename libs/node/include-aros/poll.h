/*
 * OpenSSL includes <poll.h> for Unix targets, but selects its portable
 * select() implementation when POLLIN is unavailable.  AROS currently
 * provides select()/WaitSelect rather than a public POSIX poll() contract, so
 * intentionally leave the POLL* feature macros undefined.
 */
#ifndef AROS_NODE_OPENSSL_POLL_H
#define AROS_NODE_OPENSSL_POLL_H

#endif

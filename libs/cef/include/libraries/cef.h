#ifndef LIBRARIES_CEF_H
#define LIBRARIES_CEF_H

/*
 * Public identity for cef.library.
 *
 * The ABI exposes CEF's identity queries plus its process and message-loop
 * lifecycle. Pointer arguments intentionally remain opaque here so the
 * resident library does not duplicate CEF's generated public structures.
 * Clients that use lifecycle or browser vectors compile against the matching
 * generated CEF C API headers.
 */

#include <exec/types.h>

#define CEFNAME "cef.library"
#define CEFVERSION 1

#endif /* LIBRARIES_CEF_H */

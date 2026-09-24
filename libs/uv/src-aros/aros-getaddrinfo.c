/*
 * Asynchronous AROS getaddrinfo backend.
 *
 * Keeps libuv's request/threadpool contract while calling the Roadshow
 * compatible getaddrinfo LVO exported by AROSTCP's bsdsocket.library.
 */

#include "uv.h"
#include "internal.h"
#include "idna.h"

#include <aros/libcall.h>
#include <exec/libraries.h>
#include <proto/exec.h>

#include <netdb.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static int uv__aros_getaddrinfo(const char* hostname,
                                const char* service,
                                const struct addrinfo* hints,
                                struct addrinfo** result) {
  struct Library* socket_base;
  LONG error;

  socket_base = OpenLibrary((CONST_STRPTR)"bsdsocket.library", 4);
  if (socket_base == NULL)
    return EAI_FAIL;

  error = AROS_LC4(LONG, RS_getaddrinfo,
                   AROS_LCA(const char*, hostname, A0),
                   AROS_LCA(const char*, service, A1),
                   AROS_LCA(const struct addrinfo*, hints, A2),
                   AROS_LCA(struct addrinfo**, result, A3),
                   struct Library*, socket_base, 136, UL);
  CloseLibrary(socket_base);
  return (int)error;
}

static void uv__aros_freeaddrinfo(struct addrinfo* address) {
  struct Library* socket_base;

  if (address == NULL)
    return;

  socket_base = OpenLibrary((CONST_STRPTR)"bsdsocket.library", 4);
  if (socket_base != NULL) {
    AROS_LC1NR(void, RS_freeaddrinfo,
               AROS_LCA(struct addrinfo*, address, A0),
               struct Library*, socket_base, 135, UL);
    CloseLibrary(socket_base);
    return;
  }

  while (address != NULL) {
    struct addrinfo* next = address->ai_next;
    if (address->ai_canonname != NULL)
      FreeVec(address->ai_canonname);
    FreeVec(address);
    address = next;
  }
}

int uv__getaddrinfo_translate_error(int error) {
  switch (error) {
    case 0:
      return 0;
#ifdef EAI_ADDRFAMILY
    case EAI_ADDRFAMILY:
      return UV_EAI_ADDRFAMILY;
#endif
#ifdef EAI_AGAIN
    case EAI_AGAIN:
      return UV_EAI_AGAIN;
#endif
#ifdef EAI_BADFLAGS
    case EAI_BADFLAGS:
      return UV_EAI_BADFLAGS;
#endif
#ifdef EAI_FAIL
    case EAI_FAIL:
      return UV_EAI_FAIL;
#endif
#ifdef EAI_FAMILY
    case EAI_FAMILY:
      return UV_EAI_FAMILY;
#endif
#ifdef EAI_MEMORY
    case EAI_MEMORY:
      return UV_EAI_MEMORY;
#endif
#ifdef EAI_NODATA
    case EAI_NODATA:
      return UV_EAI_NODATA;
#endif
#if defined(EAI_NONAME) && (!defined(EAI_NODATA) || EAI_NODATA != EAI_NONAME)
    case EAI_NONAME:
      return UV_EAI_NONAME;
#endif
#ifdef EAI_OVERFLOW
    case EAI_OVERFLOW:
      return UV_EAI_OVERFLOW;
#endif
#ifdef EAI_SERVICE
    case EAI_SERVICE:
      return UV_EAI_SERVICE;
#endif
#ifdef EAI_SOCKTYPE
    case EAI_SOCKTYPE:
      return UV_EAI_SOCKTYPE;
#endif
#ifdef EAI_SYSTEM
    case EAI_SYSTEM:
      return UV_EAI_FAIL;
#endif
    default:
      return UV_EAI_FAIL;
  }
}

static void uv__getaddrinfo_work(struct uv__work* work) {
  uv_getaddrinfo_t* request;
  int error;

  request = container_of(work, uv_getaddrinfo_t, work_req);
  error = uv__aros_getaddrinfo(request->hostname,
                               request->service,
                               request->hints,
                               &request->addrinfo);
  request->retcode = uv__getaddrinfo_translate_error(error);
}

static void uv__getaddrinfo_done(struct uv__work* work, int status) {
  uv_getaddrinfo_t* request;

  request = container_of(work, uv_getaddrinfo_t, work_req);
  uv__req_unregister(request->loop, request);

  if (request->hints != NULL)
    uv__free(request->hints);
  else if (request->service != NULL)
    uv__free(request->service);
  else
    uv__free(request->hostname);

  request->hints = NULL;
  request->service = NULL;
  request->hostname = NULL;

  if (status == UV_ECANCELED)
    request->retcode = UV_EAI_CANCELED;

  if (request->cb != NULL)
    request->cb(request, request->retcode, request->addrinfo);
}

int uv_getaddrinfo(uv_loop_t* loop,
                   uv_getaddrinfo_t* request,
                   uv_getaddrinfo_cb callback,
                   const char* hostname,
                   const char* service,
                   const struct addrinfo* hints) {
  char hostname_ascii[256];
  size_t hostname_length;
  size_t service_length;
  size_t hints_length;
  size_t offset;
  char* storage;
  long error;

  if (request == NULL || (hostname == NULL && service == NULL))
    return UV_EINVAL;

  if (hostname != NULL) {
    error = uv__idna_toascii(hostname,
                             hostname + strlen(hostname),
                             hostname_ascii,
                             hostname_ascii + sizeof(hostname_ascii));
    if (error < 0)
      return (int)error;
    hostname = hostname_ascii;
  }

  hostname_length = hostname != NULL ? strlen(hostname) + 1 : 0;
  service_length = service != NULL ? strlen(service) + 1 : 0;
  hints_length = hints != NULL ? sizeof(*hints) : 0;
  storage = uv__malloc(hostname_length + service_length + hints_length);
  if (storage == NULL)
    return UV_ENOMEM;

  uv__req_init(loop, request, UV_GETADDRINFO);
  request->loop = loop;
  request->cb = callback;
  request->addrinfo = NULL;
  request->hints = NULL;
  request->service = NULL;
  request->hostname = NULL;
  request->retcode = 0;

  offset = 0;
  if (hints != NULL) {
    request->hints = memcpy(storage + offset, hints, sizeof(*hints));
    offset += sizeof(*hints);
  }
  if (service != NULL) {
    request->service = memcpy(storage + offset, service, service_length);
    offset += service_length;
  }
  if (hostname != NULL)
    request->hostname = memcpy(storage + offset, hostname, hostname_length);

  if (callback != NULL) {
    uv__work_submit(loop,
                    &request->work_req,
                    UV__WORK_SLOW_IO,
                    uv__getaddrinfo_work,
                    uv__getaddrinfo_done);
    return 0;
  }

  uv__getaddrinfo_work(&request->work_req);
  uv__getaddrinfo_done(&request->work_req, 0);
  return request->retcode;
}

void uv_freeaddrinfo(struct addrinfo* address) {
  uv__aros_freeaddrinfo(address);
}

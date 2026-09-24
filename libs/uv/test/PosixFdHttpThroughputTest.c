/*
 * Plain HTTP throughput discriminator for the AROS fd.library socket path.
 *
 * This deliberately creates/connects the socket through bsdsocket.library but
 * transfers the request and response through selectable read paths.  The
 * "posix", "hook", and "recv" modes isolate descriptor dispatch from the
 * bsdsocket hook and public recv() entry without involving TLS, OpenSSL,
 * libuv, Chromium, or disk writes.
 */

#include <aros/debug.h>
#include <dos/dos.h>
#include <exec/libraries.h>
#include <libraries/fd.h>
#include <proto/bsdsocket.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/fd.h>

#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_LIMIT (32UL * 1024UL * 1024UL)
#define RX_SIZE (64UL * 1024UL)
#define HEADER_SIZE (32UL * 1024UL)
#define PROGRESS_INTERVAL (4UL * 1024UL * 1024UL)

#define TLOG(...) do {                    \
  printf("[AROS POSIX-FD TCP] " __VA_ARGS__); \
  printf("\n");                         \
  fflush(stdout);                         \
  bug("[AROS POSIX-FD TCP] " __VA_ARGS__);   \
  bug("\n");                           \
} while (0)

struct Library *SocketBase = NULL;
struct Library *FDBase = NULL;

static unsigned long long datestamp_ticks(const struct DateStamp *stamp)
{
  return ((unsigned long long)stamp->ds_Days * 24ULL * 60ULL +
          (unsigned long long)stamp->ds_Minute) *
             (unsigned long long)TICKS_PER_SECOND * 60ULL +
         (unsigned long long)stamp->ds_Tick;
}

int main(int argc, char **argv)
{
  const char *host = argc > 1 ? argv[1] : "10.203.0.1";
  const char *path = argc > 2 ? argv[2] : "/posix-fd-ab.bin";
  unsigned long limit = argc > 3 ? strtoul(argv[3], NULL, 0) : DEFAULT_LIMIT;
  unsigned long port = argc > 4 ? strtoul(argv[4], NULL, 0) : 18080UL;
  const char *read_mode = argc > 5 ? argv[5] : "posix";
  struct hostent *resolved;
  struct sockaddr_in address;
  struct DateStamp start, finish;
  char request[512];
  static char rx[RX_SIZE];
  static char headers[HEADER_SIZE + 1];
  size_t header_used = 0;
  unsigned long long body_bytes = 0, read_calls = 0, read_bytes = 0;
  unsigned long long next_progress = PROGRESS_INTERVAL;
  unsigned long read_min = RX_SIZE, read_max = 0;
  unsigned long long start_ticks, finish_ticks;
  int sock = -1, request_length, received, headers_complete = 0, rc = 20;
  unsigned long long elapsed_ticks, elapsed_ms, milli_mbps;
  const struct fd_hooks *socket_hooks = NULL;
  APTR socket_data = NULL;

  if (!limit) limit = DEFAULT_LIMIT;
  if (!port || port > 65535UL) {
    TLOG("FAIL: invalid TCP port %lu", port);
    return 20;
  }

  SocketBase = OpenLibrary("bsdsocket.library", 4);
  if (!SocketBase) {
    TLOG("FAIL: could not open bsdsocket.library v4");
    return 20;
  }
  resolved = gethostbyname(host);
  if (!resolved || !resolved->h_addr_list || !resolved->h_addr_list[0]) {
    TLOG("FAIL: DNS lookup for %s", host);
    goto cleanup;
  }
  memset(&address, 0, sizeof(address));
  address.sin_family = AF_INET;
  address.sin_port = htons((unsigned short)port);
  memcpy(&address.sin_addr, resolved->h_addr_list[0], sizeof(address.sin_addr));
  sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0 || connect(sock, (struct sockaddr *)&address, sizeof(address))) {
    TLOG("FAIL: socket/connect errno=%ld", (long)Errno());
    goto cleanup;
  }

  FDBase = OpenLibrary("fd.library", 0);
  if (FDBase) {
    struct DateStamp lookup_start, lookup_finish;
    unsigned long long lookup_ticks, lookup_us;
    unsigned long i;
    volatile IPTR lookup_sink = 0;

    DateStamp(&lookup_start);
    for (i = 0; i < 100; i++) {
      fd_owner_t owner = FD_GetOwner(sock);
      lookup_sink ^= owner;
      lookup_sink ^= (IPTR)FD_GetOwnerHooks(owner);
      lookup_sink ^= (IPTR)FD_GetData(sock);
    }
    DateStamp(&lookup_finish);
    lookup_ticks = datestamp_ticks(&lookup_finish) -
                   datestamp_ticks(&lookup_start);
    lookup_us = lookup_ticks * 1000000ULL / TICKS_PER_SECOND;
    TLOG("fd_registry_profile iterations=100 calls=300 elapsed_us=%llu "
         "average_us=%llu sink=%lu", lookup_us, lookup_us / 300ULL,
         (unsigned long)lookup_sink);

    if (!strcmp(read_mode, "hook")) {
      fd_owner_t owner = FD_GetOwner(sock);
      socket_hooks = FD_GetOwnerHooks(owner);
      socket_data = FD_GetData(sock);
      if (!socket_hooks || !socket_hooks->fdh_read || !socket_data) {
        TLOG("FAIL: direct hook unavailable owner=%lu hooks=%p data=%p",
             (unsigned long)owner, socket_hooks, socket_data);
        goto cleanup;
      }
    }
  } else {
    TLOG("fd_registry_profile unavailable");
    if (!strcmp(read_mode, "hook")) {
      TLOG("FAIL: direct hook requires fd.library");
      goto cleanup;
    }
  }

  request_length = snprintf(request, sizeof(request),
      "GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", path, host);
  if (request_length <= 0 || request_length >= (int)sizeof(request) ||
      write(sock, request, (size_t)request_length) != request_length) {
    TLOG("FAIL: POSIX write errno=%d", errno);
    goto cleanup;
  }

  TLOG("host=%s port=%lu path=%s limit=%lu read_size=%lu read_mode=%s",
       host, port, path, limit, (unsigned long)sizeof(rx), read_mode);
  DateStamp(&start);
  while (body_bytes < limit) {
    if (!strcmp(read_mode, "recv")) {
      received = recv(sock, rx, sizeof(rx), 0);
      if (received < 0)
        errno = (int)Errno();
    } else if (socket_hooks) {
      LONG hook_error = 0;
      received = (int)socket_hooks->fdh_read(socket_data, rx, sizeof(rx),
                                              &hook_error);
      if (received < 0)
        errno = hook_error;
    } else {
      received = read(sock, rx, sizeof(rx));
    }
    if (received <= 0)
      break;
    read_calls++;
    read_bytes += (unsigned long)received;
    if ((unsigned long)received < read_min) read_min = (unsigned long)received;
    if ((unsigned long)received > read_max) read_max = (unsigned long)received;

    if (!headers_complete) {
      size_t copy = (size_t)received;
      size_t previous = header_used;
      char *boundary;
      if (copy > HEADER_SIZE - header_used) copy = HEADER_SIZE - header_used;
      memcpy(headers + header_used, rx, copy);
      header_used += copy;
      headers[header_used] = '\0';
      boundary = strstr(headers, "\r\n\r\n");
      if (boundary) {
        size_t header_bytes = (size_t)(boundary + 4 - headers);
        headers_complete = 1;
        if (strncmp(headers, "HTTP/1.1 200", 12) &&
            strncmp(headers, "HTTP/1.0 200", 12)) {
          TLOG("FAIL: response was not HTTP 200: %.64s", headers);
          goto cleanup;
        }
        body_bytes = previous + (size_t)received - header_bytes;
        TLOG("response=%.15s header_bytes=%lu", headers,
             (unsigned long)header_bytes);
      } else if (copy < (size_t)received || header_used == HEADER_SIZE) {
        TLOG("FAIL: headers exceeded %lu bytes", (unsigned long)HEADER_SIZE);
        goto cleanup;
      }
    } else {
      body_bytes += (unsigned long long)received;
    }

    if (headers_complete && body_bytes >= next_progress) {
      struct DateStamp progress;
      unsigned long long progress_ticks, progress_ms, progress_milli_mbps;
      DateStamp(&progress);
      progress_ticks = datestamp_ticks(&progress) - datestamp_ticks(&start);
      if (!progress_ticks) progress_ticks = 1;
      progress_ms = progress_ticks * 1000ULL / TICKS_PER_SECOND;
      progress_milli_mbps = body_bytes * 8ULL * 1000ULL *
                            TICKS_PER_SECOND / progress_ticks / 1000000ULL;
      TLOG("progress=%llu/%lu elapsed_ms=%llu average=%llu.%03llu Mbps",
           body_bytes, limit, progress_ms, progress_milli_mbps / 1000ULL,
           progress_milli_mbps % 1000ULL);
      do next_progress += PROGRESS_INTERVAL; while (next_progress <= body_bytes);
    }
  }

  DateStamp(&finish);
  if (!headers_complete || body_bytes < limit) {
    TLOG("FAIL: short response body=%llu requested=%lu read_rc=%d errno=%d",
         body_bytes, limit, received, errno);
    goto cleanup;
  }
  start_ticks = datestamp_ticks(&start);
  finish_ticks = datestamp_ticks(&finish);
  elapsed_ticks = finish_ticks - start_ticks;
  if (!elapsed_ticks) elapsed_ticks = 1;
  elapsed_ms = elapsed_ticks * 1000ULL / TICKS_PER_SECOND;
  milli_mbps = body_bytes * 8ULL * 1000ULL * TICKS_PER_SECOND /
               elapsed_ticks / 1000000ULL;
  TLOG("read_shape calls=%llu bytes=%llu min=%lu max=%lu average=%llu",
       read_calls, read_bytes, read_min, read_max,
       read_calls ? read_bytes / read_calls : 0ULL);
  TLOG("PASS: bytes=%llu elapsed_ms=%llu throughput=%llu.%03llu Mbps",
       body_bytes, elapsed_ms, milli_mbps / 1000ULL, milli_mbps % 1000ULL);
  rc = 0;

cleanup:
  if (sock >= 0) CloseSocket(sock);
  if (FDBase) CloseLibrary(FDBase);
  CloseLibrary(SocketBase);
  return rc;
}

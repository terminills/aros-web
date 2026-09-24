/*
    UVHttpThroughputTest -- AROS TAP/TCP download-throughput discriminator.

    Despite living beside the libuv network tests, this probe deliberately uses
    bsdsocket.library directly.  It measures the native AROS TCP path without
    Chromium, Blink, V8, TLS, disk writes, or libuv scheduling in the result.

    The default endpoint is speedtest.tele2.net:80/100MB.zip.  The response body
    is discarded after 32 MiB so the test is bounded while still long enough to
    escape connection-startup noise.  Optional arguments override host, path,
    byte limit, TCP port, and requested SO_RCVBUF, in that order.  The receive
    buffer override is diagnostic; zero leaves the stack default unchanged.
*/

#include <aros/debug.h>
#include <dos/dos.h>
#include <exec/libraries.h>
#include <proto/bsdsocket.h>
#include <proto/dos.h>
#include <proto/exec.h>

#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_HOST "speedtest.tele2.net"
#define DEFAULT_PATH "/100MB.zip"
#define DEFAULT_LIMIT (32UL * 1024UL * 1024UL)
#define RX_SIZE (64UL * 1024UL)
#define HEADER_SIZE (32UL * 1024UL)
#define PROGRESS_INTERVAL (1UL * 1024UL * 1024UL)

#define TLOG(...)                                                           \
  do {                                                                      \
    printf("[AROS TCP] " __VA_ARGS__);                                     \
    printf("\n");                                                          \
    fflush(stdout);                                                         \
    bug("[AROS TCP] " __VA_ARGS__);                                        \
    bug("\n");                                                             \
  } while (0)

struct Library* SocketBase = NULL;

static unsigned long long datestamp_ticks(const struct DateStamp* stamp) {
  return ((unsigned long long)stamp->ds_Days * 24ULL * 60ULL +
          (unsigned long long)stamp->ds_Minute) *
             (unsigned long long)TICKS_PER_SECOND * 60ULL +
         (unsigned long long)stamp->ds_Tick;
}

int main(int argc, char** argv) {
  const char* host = argc > 1 ? argv[1] : DEFAULT_HOST;
  const char* path = argc > 2 ? argv[2] : DEFAULT_PATH;
  unsigned long limit = argc > 3 ? strtoul(argv[3], NULL, 0) : DEFAULT_LIMIT;
  unsigned long port = argc > 4 ? strtoul(argv[4], NULL, 0) : 80UL;
  unsigned long requested_receive_buffer =
      argc > 5 ? strtoul(argv[5], NULL, 0) : 0UL;
  struct hostent* resolved;
  struct sockaddr_in address;
  struct DateStamp start;
  struct DateStamp finish;
  char request[512];
  static char rx[RX_SIZE];
  static char headers[HEADER_SIZE + 1];
  size_t header_used = 0;
  unsigned long long body_bytes = 0;
  unsigned long long receive_calls = 0;
  unsigned long long receive_bytes = 0;
  unsigned long receive_min = RX_SIZE;
  unsigned long receive_max = 0;
  unsigned long long receive_le_1460 = 0;
  unsigned long long receive_le_4096 = 0;
  unsigned long long receive_le_16384 = 0;
  unsigned long long receive_gt_16384 = 0;
  unsigned long long next_progress = PROGRESS_INTERVAL;
  unsigned long long start_ticks;
  unsigned long long finish_ticks;
  int sock = -1;
  int receive_buffer = 0;
  int receive_buffer_length = sizeof(receive_buffer);
  int request_length;
  int received;
  int rc = 20;
  int headers_complete = 0;

  if (limit == 0) {
    limit = DEFAULT_LIMIT;
  }
  if (port == 0 || port > 65535UL) {
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
  if (sock < 0) {
    TLOG("FAIL: socket errno=%ld", (long)Errno());
    goto cleanup;
  }
  if (requested_receive_buffer != 0) {
    int requested = (int)requested_receive_buffer;
    if (requested_receive_buffer > 0x7fffffffUL ||
        setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &requested,
                   sizeof(requested)) != 0) {
      TLOG("FAIL: SO_RCVBUF request=%lu errno=%ld", requested_receive_buffer,
           (long)Errno());
      goto cleanup;
    }
  }
  if (connect(sock, (struct sockaddr*)&address, sizeof(address)) != 0) {
    TLOG("FAIL: connect errno=%ld", (long)Errno());
    goto cleanup;
  }

  if (getsockopt(sock, SOL_SOCKET, SO_RCVBUF, &receive_buffer,
                 &receive_buffer_length) != 0) {
    receive_buffer = -1;
  }

  request_length = snprintf(request, sizeof(request),
                            "GET %s HTTP/1.1\r\n"
                            "Host: %s\r\n"
                            "User-Agent: AROS-TCP-Throughput/1\r\n"
                            "Accept: */*\r\n"
                            "Connection: close\r\n\r\n",
                            path, host);
  if (request_length <= 0 || request_length >= (int)sizeof(request) ||
      send(sock, request, request_length, 0) != request_length) {
    TLOG("FAIL: send errno=%ld", (long)Errno());
    goto cleanup;
  }

  TLOG("host=%s port=%lu path=%s limit=%lu requested_receive_buffer=%lu "
       "receive_buffer=%d",
       host, port, path, limit, requested_receive_buffer, receive_buffer);
  DateStamp(&start);

  while (body_bytes < limit && (received = recv(sock, rx, sizeof(rx), 0)) > 0) {
    receive_calls++;
    receive_bytes += (unsigned long)received;
    if ((unsigned long)received < receive_min) {
      receive_min = (unsigned long)received;
    }
    if ((unsigned long)received > receive_max) {
      receive_max = (unsigned long)received;
    }
    if (received <= 1460) {
      receive_le_1460++;
    } else if (received <= 4096) {
      receive_le_4096++;
    } else if (received <= 16384) {
      receive_le_16384++;
    } else {
      receive_gt_16384++;
    }

    if (!headers_complete) {
      size_t copy = (size_t)received;
      size_t previous_header_used = header_used;
      char* boundary;
      if (copy > HEADER_SIZE - header_used) {
        copy = HEADER_SIZE - header_used;
      }
      memcpy(headers + header_used, rx, copy);
      header_used += copy;
      headers[header_used] = '\0';
      boundary = strstr(headers, "\r\n\r\n");
      if (boundary) {
        size_t header_bytes = (size_t)(boundary + 4 - headers);
        headers_complete = 1;
        if (strncmp(headers, "HTTP/1.1 200", 12) != 0 &&
            strncmp(headers, "HTTP/1.0 200", 12) != 0) {
          TLOG("FAIL: response was not HTTP 200: %.64s", headers);
          goto cleanup;
        }
        body_bytes = previous_header_used + (size_t)received - header_bytes;
        TLOG("response=%.15s header_bytes=%lu", headers,
             (unsigned long)header_bytes);
      } else if (copy < (size_t)received || header_used == HEADER_SIZE) {
        TLOG("FAIL: HTTP response headers exceeded %lu bytes",
             (unsigned long)HEADER_SIZE);
        goto cleanup;
      }
    } else {
      body_bytes += (unsigned long long)received;
    }

    if (headers_complete && body_bytes >= next_progress) {
      struct DateStamp progress;
      unsigned long long progress_ticks;
      unsigned long long progress_centiseconds;
      unsigned long long progress_kbps;

      DateStamp(&progress);
      progress_ticks = datestamp_ticks(&progress) - datestamp_ticks(&start);
      progress_centiseconds =
          progress_ticks * 100ULL / (unsigned long long)TICKS_PER_SECOND;
      if (progress_ticks == 0) {
        progress_ticks = 1;
      }
      progress_kbps = body_bytes * 8ULL * (unsigned long long)TICKS_PER_SECOND /
                      progress_ticks / 1000ULL;
      TLOG("progress=%llu/%lu bytes centiseconds=%llu average_kbps=%llu",
           body_bytes, limit, progress_centiseconds, progress_kbps);
      do {
        next_progress += PROGRESS_INTERVAL;
      } while (next_progress <= body_bytes);
    }
  }

  DateStamp(&finish);
  if (!headers_complete) {
    TLOG("FAIL: connection ended before complete HTTP headers");
    goto cleanup;
  }
  if (body_bytes < limit) {
    TLOG("FAIL: short response body=%llu requested=%lu errno=%ld", body_bytes,
         limit, (long)Errno());
    goto cleanup;
  }

  start_ticks = datestamp_ticks(&start);
  finish_ticks = datestamp_ticks(&finish);
  TLOG("recv_shape calls=%llu bytes=%llu min=%lu max=%lu average=%llu "
       "le1460=%llu le4096=%llu le16384=%llu gt16384=%llu",
       receive_calls, receive_bytes, receive_min, receive_max,
       receive_calls ? receive_bytes / receive_calls : 0ULL,
       receive_le_1460, receive_le_4096, receive_le_16384,
       receive_gt_16384);
  TLOG("PASS: bytes=%llu ticks=%llu centiseconds=%llu throughput_kbps=%llu",
       body_bytes, finish_ticks - start_ticks,
       (finish_ticks - start_ticks) * 100ULL /
           (unsigned long long)TICKS_PER_SECOND,
       body_bytes * 8ULL * (unsigned long long)TICKS_PER_SECOND /
           (finish_ticks - start_ticks ? finish_ticks - start_ticks : 1ULL) /
           1000ULL);
  rc = 0;

cleanup:
  if (sock >= 0) {
    CloseSocket(sock);
  }
  CloseLibrary(SocketBase);
  return rc;
}

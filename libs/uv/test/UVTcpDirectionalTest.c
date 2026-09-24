/*
    UVTcpDirectionalTest -- raw AROS TAP/TCP directional discriminator.

    This direct bsdsocket.library probe deliberately avoids HTTP, TLS, disk
    writes, libuv, Chromium, and V8.  "send" measures AROS -> host and
    "receive" measures host -> AROS, matching the important directional
    distinction in an iperf test.

    Usage:
      UVTcpDirectionalTest send <host> <port> [bytes]
      UVTcpDirectionalTest receive <port> [bytes]
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

#define DEFAULT_LIMIT (256UL * 1024UL * 1024UL)
#define BUFFER_SIZE (64UL * 1024UL)
#define PROGRESS_INTERVAL (8UL * 1024UL * 1024UL)

#define TLOG(...)                                                           \
  do {                                                                      \
    printf("[AROS RAW TCP] " __VA_ARGS__);                                 \
    printf("\n");                                                          \
    fflush(stdout);                                                         \
    bug("[AROS RAW TCP] " __VA_ARGS__);                                    \
    bug("\n");                                                            \
  } while (0)

struct Library* SocketBase = NULL;

static unsigned long long datestamp_ticks(const struct DateStamp* stamp) {
  return ((unsigned long long)stamp->ds_Days * 24ULL * 60ULL +
          (unsigned long long)stamp->ds_Minute) *
             (unsigned long long)TICKS_PER_SECOND * 60ULL +
         (unsigned long long)stamp->ds_Tick;
}

static void report_progress(const char* direction,
                            unsigned long long bytes,
                            unsigned long limit,
                            const struct DateStamp* start) {
  struct DateStamp now;
  double seconds;
  double mbps;

  DateStamp(&now);
  seconds = (double)(datestamp_ticks(&now) - datestamp_ticks(start)) /
            (double)TICKS_PER_SECOND;
  if (seconds <= 0.0) {
    seconds = 1.0 / (double)TICKS_PER_SECOND;
  }
  mbps = ((double)bytes * 8.0) / seconds / 1000000.0;
  TLOG("direction=%s progress=%llu/%lu bytes seconds=%.2f average=%.3f Mbps",
       direction, bytes, limit, seconds, mbps);
}

static int run_stream(int sock,
                      int sending,
                      unsigned long limit,
                      int socket_buffer) {
  static unsigned char buffer[BUFFER_SIZE];
  struct DateStamp start;
  struct DateStamp finish;
  unsigned long long total = 0;
  unsigned long long calls = 0;
  unsigned long long next_progress = PROGRESS_INTERVAL;
  unsigned long transfer_min = BUFFER_SIZE;
  unsigned long transfer_max = 0;
  double seconds;
  double mbps;
  int transferred;

  memset(buffer, 0xa5, sizeof(buffer));
  TLOG("direction=%s limit=%lu socket_buffer=%d chunk=%lu",
       sending ? "aros-to-host" : "host-to-aros", limit, socket_buffer,
       (unsigned long)sizeof(buffer));
  DateStamp(&start);

  while (total < limit) {
    unsigned long remaining = limit - (unsigned long)total;
    unsigned long chunk = remaining < sizeof(buffer) ? remaining : sizeof(buffer);

    if (sending) {
      transferred = send(sock, buffer, chunk, 0);
    } else {
      transferred = recv(sock, buffer, chunk, 0);
    }
    if (transferred <= 0) {
      TLOG("FAIL: direction=%s transfer=%d total=%llu errno=%ld",
           sending ? "aros-to-host" : "host-to-aros", transferred, total,
           (long)Errno());
      return 20;
    }

    calls++;
    total += (unsigned long)transferred;
    if ((unsigned long)transferred < transfer_min) {
      transfer_min = (unsigned long)transferred;
    }
    if ((unsigned long)transferred > transfer_max) {
      transfer_max = (unsigned long)transferred;
    }
    if (total >= next_progress) {
      report_progress(sending ? "aros-to-host" : "host-to-aros", total,
                      limit, &start);
      do {
        next_progress += PROGRESS_INTERVAL;
      } while (next_progress <= total);
    }
  }

  DateStamp(&finish);
  seconds = (double)(datestamp_ticks(&finish) - datestamp_ticks(&start)) /
            (double)TICKS_PER_SECOND;
  if (seconds <= 0.0) {
    seconds = 1.0 / (double)TICKS_PER_SECOND;
  }
  mbps = ((double)total * 8.0) / seconds / 1000000.0;
  TLOG("transfer_shape direction=%s calls=%llu min=%lu max=%lu average=%.1f",
       sending ? "aros-to-host" : "host-to-aros", calls, transfer_min,
       transfer_max, calls ? (double)total / (double)calls : 0.0);
  TLOG("PASS: direction=%s bytes=%llu seconds=%.3f throughput=%.3f Mbps",
       sending ? "aros-to-host" : "host-to-aros", total, seconds, mbps);
  return 0;
}

static int run_sender(const char* host, unsigned long port,
                      unsigned long limit) {
  struct hostent* resolved;
  struct sockaddr_in address;
  int sock = -1;
  int send_buffer = 0;
  int option_length = sizeof(send_buffer);
  int rc = 20;

  resolved = gethostbyname(host);
  if (!resolved || !resolved->h_addr_list || !resolved->h_addr_list[0]) {
    TLOG("FAIL: DNS lookup for %s", host);
    return 20;
  }
  memset(&address, 0, sizeof(address));
  address.sin_family = AF_INET;
  address.sin_port = htons((unsigned short)port);
  memcpy(&address.sin_addr, resolved->h_addr_list[0], sizeof(address.sin_addr));

  sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) {
    TLOG("FAIL: sender socket errno=%ld", (long)Errno());
    return 20;
  }
  if (connect(sock, (struct sockaddr*)&address, sizeof(address)) != 0) {
    TLOG("FAIL: connect host=%s port=%lu errno=%ld", host, port,
         (long)Errno());
    goto cleanup;
  }
  if (getsockopt(sock, SOL_SOCKET, SO_SNDBUF, &send_buffer, &option_length) != 0) {
    send_buffer = -1;
  }
  rc = run_stream(sock, 1, limit, send_buffer);

cleanup:
  CloseSocket(sock);
  return rc;
}

static int run_receiver(unsigned long port, unsigned long limit) {
  struct sockaddr_in address;
  int listener = -1;
  int client = -1;
  int receive_buffer = 0;
  int option_length = sizeof(receive_buffer);
  int reuse = 1;
  int rc = 20;

  listener = socket(AF_INET, SOCK_STREAM, 0);
  if (listener < 0) {
    TLOG("FAIL: listener socket errno=%ld", (long)Errno());
    return 20;
  }
  setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
  memset(&address, 0, sizeof(address));
  address.sin_family = AF_INET;
  address.sin_port = htons((unsigned short)port);
  address.sin_addr.s_addr = htonl(INADDR_ANY);
  if (bind(listener, (struct sockaddr*)&address, sizeof(address)) != 0 ||
      listen(listener, 1) != 0) {
    TLOG("FAIL: bind/listen port=%lu errno=%ld", port, (long)Errno());
    goto cleanup;
  }
  TLOG("listening port=%lu direction=host-to-aros", port);
  client = accept(listener, NULL, NULL);
  if (client < 0) {
    TLOG("FAIL: accept errno=%ld", (long)Errno());
    goto cleanup;
  }
  if (getsockopt(client, SOL_SOCKET, SO_RCVBUF, &receive_buffer,
                 &option_length) != 0) {
    receive_buffer = -1;
  }
  rc = run_stream(client, 0, limit, receive_buffer);

cleanup:
  if (client >= 0) {
    CloseSocket(client);
  }
  CloseSocket(listener);
  return rc;
}

int main(int argc, char** argv) {
  unsigned long port;
  unsigned long limit;
  int rc;

  if (argc < 3) {
    printf("Usage: %s send <host> <port> [bytes]\n", argv[0]);
    printf("       %s receive <port> [bytes]\n", argv[0]);
    return 20;
  }
  SocketBase = OpenLibrary("bsdsocket.library", 4);
  if (!SocketBase) {
    TLOG("FAIL: could not open bsdsocket.library v4");
    return 20;
  }

  if (strcmp(argv[1], "send") == 0 && argc >= 4) {
    port = strtoul(argv[3], NULL, 0);
    limit = argc > 4 ? strtoul(argv[4], NULL, 0) : DEFAULT_LIMIT;
    rc = run_sender(argv[2], port, limit);
  } else if (strcmp(argv[1], "receive") == 0) {
    port = strtoul(argv[2], NULL, 0);
    limit = argc > 3 ? strtoul(argv[3], NULL, 0) : DEFAULT_LIMIT;
    rc = run_receiver(port, limit);
  } else {
    TLOG("FAIL: invalid mode or arguments");
    rc = 20;
  }

  CloseLibrary(SocketBase);
  return rc;
}

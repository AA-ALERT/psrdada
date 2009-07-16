/*
 * Generic functions for udp sockets
 */

#include <errno.h>
#include "dada_udp.h"

#define KERNEL_BUFFER_SIZE_MAX     67108864
#define KERNEL_BUFFER_SIZE_DEFAULT 131071
#define STATS_INIT = {0, 0, 0, 0}

stats_t * init_stats_t()
{
  stats_t * s = (stats_t *) malloc(sizeof(stats_t));

  s->received = 0;
  s->dropped = 0;
  s->received_per_sec = 0;
  s->dropped_per_sec = 0;

  return s;
}

/*
 * Creates a UDP socket with the following parameters:
 *
 *   log    multilog to print messages to
 *   iface  interface to open socket on
 *   port   port to listen on
 *
 *   set the buffer size to 64MB
 *   prints output errors to log
 *   return a fd of the opened socket
 */
int dada_udp_sock_in(multilog_t* log, const char* iface, int port, int verbose)
{

  int fd = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);

  if (fd < 0) {
    multilog(log, LOG_ERR, "Could not created UDP socket: %s\n",
             strerror(errno));
    return -1;
  }

  if (verbose) 
    multilog(log, LOG_INFO, "Created UDP socket\n");

  struct sockaddr_in udp_sock;
  bzero(&(udp_sock.sin_zero), 8);                     // clear the struct
  udp_sock.sin_family = AF_INET;                      // internet/IP
  udp_sock.sin_port = htons(port);                    // set the port number
  if (strcmp(iface,"any") == 0) {
    udp_sock.sin_addr.s_addr = htonl(INADDR_ANY);     // from any interface
  } else {
    udp_sock.sin_addr.s_addr = inet_addr(iface);  // from a specific IP address
  }

  if (bind(fd, (struct sockaddr *)&udp_sock, sizeof(udp_sock)) == -1) {
    multilog (log, LOG_ERR, "Error binding UDP socket: %s\n", strerror(errno));
    return -1;
  }

  if (verbose) 
    multilog(log, LOG_INFO, "UDP socket bound to %s:%d\n", iface, port);

  return fd;

}

/* set the buffer sizes on the udp socket */
int dada_udp_sock_set_buffer_size (multilog_t* log, int fd, int verbose) {

  const int std_buffer_size = KERNEL_BUFFER_SIZE_DEFAULT;
  const int pref_buffer_size = KERNEL_BUFFER_SIZE_MAX;

  int len = 0;
  int value = pref_buffer_size;
  int retval = 0;

  // try setting the buffer to the maximum, warn if we cant
  len = sizeof(value);
  retval = setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &value, len);
  if (retval != 0) {
    perror("setsockopt SO_RCVBUF");
    return -1;
  } 
  
  // now check if it worked
  len = sizeof(value);
  value = 0;
  retval = getsockopt(fd, SOL_SOCKET, SO_RCVBUF, &value, (socklen_t *) &len);
  if (retval != 0) {
    perror("getsockopt SO_RCVBUF");
    return -1;
  } 
  
  // If we could not set the buffer to the desired size, warn...
  if (value/2 != pref_buffer_size) {
    multilog (log, LOG_WARNING, "Warning. Failed to set udp socket's "
              "buffer size to: %d, falling back to default size: %d\n",
              pref_buffer_size, std_buffer_size);

    len = sizeof(value);
    value = std_buffer_size;
    retval = setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &value, len);
    if (retval != 0) {
      perror("setsockopt SO_RCVBUF");
      return -1;
    }

    // Now double check that the buffer size is at least correct here
    len = sizeof(value);
    value = 0;
    retval = getsockopt(fd, SOL_SOCKET, SO_RCVBUF, &value,
                        (socklen_t *) &len);
    if (retval != 0) {
      perror("getsockopt SO_RCVBUF");
      return -1;
    }


    // If we could not set the buffer to the desired size, warn...
    if (value/2 != std_buffer_size) {
      multilog (log, LOG_WARNING, "Warning. Failed to set udp socket's "
                "buffer size to: %d\n", std_buffer_size);
    }

  } else {

    if (verbose)
      multilog(log, LOG_INFO, "UDP socket buffer size set to %d\n", pref_buffer_size);

  }

  return 0;

}


/* Creates a socket for transmitting UDP packets*/
int dada_udp_sock_out(int *fd, struct sockaddr_in * dagram, char *client, 
                      int port, int bcast, const char * bcast_addr) {

  /* Setup the socket for UDP packets */
  *fd = socket(PF_INET, SOCK_DGRAM, 0);
  if (*fd < 0) {
    perror("failed to create UDP socket");
    return(1);
  }

  /* If broadcasting across the entire subnet */
  if (bcast) {
    int yes = 1;
    if (setsockopt(*fd, SOL_SOCKET, SO_BROADCAST, &yes, sizeof yes) < 0) {
      perror("Could not SO_BROADCAST");
      return(1);
    }
  }

  /* Setup the UDP socket parameters*/
  struct in_addr *addr;
  dagram->sin_family = AF_INET;    // host byte order
  dagram->sin_port = htons(port);  // short, network byte order
  
  /* If we are broadcasting */
  if (bcast) {
    dagram->sin_addr.s_addr = inet_addr (bcast_addr);

  /* Else packets direct to one host */
  } else {
    addr = atoaddr(client);
    dagram->sin_addr.s_addr = addr->s_addr;
  }

  bzero(&(dagram->sin_zero), 8);       /* zero the rest of the struct */

  return 0;
}

struct in_addr *atoaddr(char *address) {
  struct hostent *host;
  static struct in_addr saddr;

  /* First try it as aaa.bbb.ccc.ddd. */
  saddr.s_addr = inet_addr(address);
  if ((int) saddr.s_addr != -1) {
    return &saddr;
  }
  host = gethostbyname(address);
  if (host != NULL) {
    return (struct in_addr *) *host->h_addr_list;
  }
  return NULL;
}


/* 
 *  receive a udp packet 
 */
size_t dada_sock_recv (int fd, char* buffer, size_t size, int flags)
{ 

  size_t received = 0;
  received = recvfrom (fd, buffer, size, 0, NULL, NULL);

  if (received < 0) {
    perror ("sock_recv recvfrom");
    return -1;
  } 
  if (received == 0) {
    fprintf (stderr, "sock_recv received zero bytes\n");
  }

  return received;
}

/* 
 *  send data in a udp packet
 */
size_t dada_sock_send(int fd, struct sockaddr_in addr, char *data, size_t size) {

  size_t numbytes;
  size_t socksize = sizeof(struct sockaddr);

  if ((numbytes=sendto(fd, data, size, 0, (struct sockaddr *)&addr, socksize)) < 0) {
    perror("sendto");
    exit(1);
  }

  return numbytes;
}

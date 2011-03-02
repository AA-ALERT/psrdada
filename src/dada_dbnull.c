#include "dada_client.h"
#include "dada_hdu.h"
#include "dada_def.h"

#include "node_array.h"
#include "string_array.h"
#include "ascii_header.h"
#include "daemon.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#include <sys/types.h>
#include <sys/socket.h>

void usage()
{
  fprintf (stdout,
     "dada_dbnull [options]\n"
     " -k key     connect to key data block\n"
     " -v         be verbose\n"
     " -q         be quiet\n"
     " -x mbytes  transfer size MB [default 64]\n"
     " -X mbytes  transfer size MiB [default 64]\n"
     " -o mbytes  transfer block sizei MB [default 8]\n"
     " -O mbytes  transfer block size Mi [default 8]\n"
     " -s         quit at end of data\n"
     " -z         use zero copy direct shm access\n"
     " -d         run as daemon\n");
}


/*! Function that opens the data transfer target */
int sock_open_function (dada_client_t* client)
{
  /* status and error logging facility */
  multilog_t* log;

  /* the header */
  char* header = 0;

  assert (client != 0);

  log = client->log;
  assert (log != 0);

  header = client->header;
  assert (header != 0);

  /* multilog (log, LOG_INFO, "Ready for writing %"PRIu64" bytes\n", xfer_size); */

  client->fd = 1;

  return 0;
}

/*! Function that closes the data file */
int sock_close_function (dada_client_t* client, uint64_t bytes_written)
{

  /* status and error logging facility */
  multilog_t* log;

  assert (client != 0);

  log = client->log;
  assert (log != 0);

  if (bytes_written < client->transfer_bytes) {
    if (!client->quiet)
      multilog (log, LOG_INFO, "Transfer stopped early at %"PRIu64" bytes\n",
	              bytes_written);
  }

  return 0;
}

/*! Pointer to the function that transfers data to/from the target */
int64_t sock_send_function (dada_client_t* client, 
			    void* data, uint64_t data_size)
{
  return data_size;
}

/*! Pointer to the function that transfers data to/from the target */
int64_t sock_send_block_function (dada_client_t* client,
              void* data, uint64_t data_size, uint64_t block_id)
{
    return data_size;
}

int main (int argc, char **argv)
{

  /* DADA Header plus Data Unit */
  dada_hdu_t* hdu = 0;

  /* DADA Primary Read Client main loop */
  dada_client_t* client = 0;

  /* DADA Logger */
  multilog_t* log = 0;

  /* Flag set in daemon mode */
  char daemon = 0;

  /* Flag set in verbose mode */
  char verbose = 0;

  /* Flag set in quiet mode */
  char quiet = 0;

  /* Quit flag */
  char quit = 0;

  /* optimal mbytes to transfer in */
  int optimal_mbytes = 8;

  /* transfer size */
  int transfer_size_mbytes = 64;

  /* transfer byte multiplier */
  int byte_base = 1024*1024;

  /* dada key for SHM */
  key_t dada_key = DADA_DEFAULT_BLOCK_KEY;

  /* zero copy direct block access */
  char zero_copy = 0;

  int arg = 0;

  /* TODO the amount to conduct a busy sleep inbetween clearing each sub
   * block */
  int busy_sleep = 0;

  while ((arg=getopt(argc,argv,"dN:vk:o:O:qsx:X:z")) != -1)
    switch (arg) {
      
    case 'd':
      daemon=1;
      break;

    case 'b':
      busy_sleep = atoi(optarg);
      break;
      
    case 'v':
      verbose=1;
      break;

    case 'k':
      if (sscanf (optarg, "%x", &dada_key) != 1) {
        fprintf (stderr, "dada_db: could not parse key from %s\n", optarg);
        return -1;
      }
      break;

    case 'o':
      optimal_mbytes = atoi(optarg);
      break;

    case 'O':
      optimal_mbytes = atoi(optarg);
      byte_base = 1000000;
      break;

    case 'q':
      quiet = 1;
      break;

    case 's':
      quit = 1;
      break;

    case 'x':
      transfer_size_mbytes = atoi(optarg);
      break;

    case 'X':
      transfer_size_mbytes = atoi(optarg);
      byte_base = 1000000;
      break;

    case 'z':
      zero_copy = 1;
      break;

    default:
      usage ();
      return 0;
      
    }

  log = multilog_open ("dada_dbnull", daemon);

  if (daemon) {
    be_a_daemon ();
    multilog_serve (log, DADA_DEFAULT_DBNULL_LOG);
  }
  else
    multilog_add (log, stderr);

  hdu = dada_hdu_create (log);

  dada_hdu_set_key(hdu, dada_key);

  if (dada_hdu_connect (hdu) < 0)
    return EXIT_FAILURE;

  if (dada_hdu_lock_read (hdu) < 0)
    return EXIT_FAILURE;

  client = dada_client_create ();

  client->log = log;

  client->data_block = hdu->data_block;
  client->header_block = hdu->header_block;

  client->open_function  = sock_open_function;

  if (zero_copy)
    client->io_block_function = sock_send_block_function;

  client->io_function    = sock_send_function;
  client->close_function = sock_close_function;
  client->direction      = dada_client_reader;

  client->transfer_bytes = transfer_size_mbytes * byte_base;
  client->optimal_bytes  = optimal_mbytes * byte_base;
  client->quiet          = quiet;

  while (!client->quit) {
    
    if (dada_client_read (client) < 0)
      multilog (log, LOG_ERR, "Error during transfer\n");

    if (quit)
      client->quit = 1;

  }

  if (dada_hdu_unlock_read (hdu) < 0)
    return EXIT_FAILURE;

  if (dada_hdu_disconnect (hdu) < 0)
    return EXIT_FAILURE;

  return EXIT_SUCCESS;
}

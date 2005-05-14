#include "dada_def.h"
#include "ipcbuf.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>


void usage ()
{
  fprintf (stdout,
	   "dada_db - create or destroy the DADA shared memory ring buffer\n"

	   "USAGE: dada_db [-d] [-n nbufs] [-b bufsz]\n"
	   "WHERE:\n"
	   " -n  number of buffers in ring      [default: %"PRIu64"]\n"
	   " -b  size of each buffer (in bytes) [default: %"PRIu64"]\n"
	   " -d  destroy the shared memory area [default: create]\n"
	   " -l  lock the shared memory area in physical RAM\n", 
	   DADA_DEFAULT_BLOCK_NUM, DADA_DEFAULT_BLOCK_SIZE);
}

int main (int argc, char** argv)
{
  uint64_t nbufs = DADA_DEFAULT_BLOCK_NUM;
  uint64_t bufsz = DADA_DEFAULT_BLOCK_SIZE;
  uint64_t hdrsz = DADA_DEFAULT_HEADER_SIZE;

  key_t data_key = DADA_DATA_BLOCK_KEY;
  key_t hdr_key  = DADA_HEADER_BLOCK_KEY;

  ipcbuf_t data_block = IPCBUF_INIT;
  ipcbuf_t header = IPCBUF_INIT;


  int destroy = 0;
  int lock = 0;
  int arg;

  while ((arg = getopt(argc, argv, "hdn:b:l")) != -1) {

    switch (arg)  {
    case 'h':
      usage ();
      return 0;

    case 'd':
      destroy = 1;
      break;

    case 'n':
      sscanf (optarg, "%"PRIu64"", &nbufs);
      break;

    case 'b':
      sscanf (optarg, "%"PRIu64"", &bufsz);
      break;

    case 'l':
      lock = 1;
      break;
    }
  }

  if (destroy) {

    ipcbuf_connect (&data_block, data_key);
    ipcbuf_destroy (&data_block);

    ipcbuf_connect (&header, hdr_key);
    ipcbuf_destroy (&header);

    fprintf (stderr, "Destroyed DADA data and header blocks\n");

    return 0;
  }

  if (ipcbuf_create (&data_block, data_key, nbufs, bufsz) < 0) {
    fprintf (stderr, "Could not create DADA data block\n");
    return -1;
  }

  fprintf (stderr, "Created DADA data block with"
	   " nbufs=%"PRIu64" bufsz=%"PRIu64"\n", nbufs, bufsz);

  if (ipcbuf_create (&header, hdr_key, 1, hdrsz) < 0) {
    fprintf (stderr, "Could not create DADA header block\n");
    return -1;
  }

  fprintf (stderr, "Created DADA header block with %"PRIu64" bytes\n", hdrsz);

  if (lock && ipcbuf_lock (&data_block) < 0) {
    fprintf (stderr, "Could not lock DADA data block into RAM\n");
    return -1;
  }

  if (lock && ipcbuf_lock (&header) < 0) {
    fprintf (stderr, "Could not lock DADA header block into RAM\n");
    return -1;
  }

  return 0;
}

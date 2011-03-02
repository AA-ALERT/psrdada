#include "dada_client.h"
#include "dada_hdu.h"
#include "dada_def.h"

#include "ascii_header.h"
#include "daemon.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <assert.h>
#include <math.h>

#include <sys/types.h>
#include <sys/stat.h>

void usage()
{
  fprintf (stdout,
           "caspsr_dbdecidb [options] in_key out_key\n"
           " -n zone   move to the specifed nyquist zone [default 0]\n"
           " -t num    decimation factor [default 2]\n"
           " -s        1 transfer, then exit\n"
           " -z        use zero copy transfers\n"
           " -v        verbose mode\n");
}

typedef struct {

  // output DADA key
  key_t key;

  // output HDU
  dada_hdu_t * hdu;

  // decimation factor
  unsigned tdec;

  // number of bytes read
  uint64_t bytes_in;

  // number of bytes written
  uint64_t bytes_out;

  // verbose output
  int verbose;

  // number of bytes of consective pol
  int resolution;

  // nyquist zone to move to
  int nyquist_zone;

  uint64_t out_block_size;

  char * curr_block;

  unsigned block_open;

  uint64_t outdat;

  uint64_t bytes_written;

  unsigned idec;

  unsigned ocnt;

} caspsr_dbdecidb_t;

#define CASPSR_DBDECIDB_INIT { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }

/*! Function that opens the data transfer target */
int dbdecidb_open (dada_client_t* client)
{

  // the caspsr_dbdecidb specific data
  caspsr_dbdecidb_t* ctx = 0;

  // status and error logging facilty
  multilog_t* log = 0;

  // header to copy from in to out
  char * header = 0;

  // header parameters that will be adjusted
  double old_bw = 0;
  double old_cf = 0;
  double old_tsamp = 0;
  double old_bytesps = 0;
  double new_bw = 0;
  double new_cf = 0;
  double new_tsamp = 0;
  double new_bytesps = 0;
  uint64_t old_filesize = 0;
  uint64_t new_filesize = 0;

  assert (client != 0);

  log = client->log;
  assert (log != 0);

  ctx = (caspsr_dbdecidb_t *) client->context;
  assert (ctx != 0);

  // lock writer status on the out HDU
  if (dada_hdu_lock_write (ctx->hdu) < 0)
  {
    multilog (log, LOG_ERR, "cannot lock write DADA HDU (key=%x)\n", ctx->key);
    return -1;
  }

  // make the required adjustments to the header parameters
  if (ascii_header_get (client->header, "BANDWIDTH", "%lf", &old_bw) != 1) {
    old_bw = 400;
    multilog (log, LOG_WARNING, "header with no BANDWIDTH, using %lf\n", old_bw);
  }

  if (ascii_header_get (client->header, "CFREQ", "%lf", &old_cf) != 1) {
    old_cf = 628.00;
    multilog (log, LOG_WARNING, "header with no CFREQ, using %lf\n", old_cf);
  }

  if (ascii_header_get (client->header, "TSAMP", "%lf", &old_tsamp) != 1) {
    old_tsamp = 0.00125;
    if (old_bw != 0)
      old_tsamp = 1 / (old_bw * 2.0);
    multilog (log, LOG_WARNING, "header with no TSAMP, using %lf\n", old_tsamp);
  }

  if (ascii_header_get (client->header, "BYTES_PER_SECOND", "%lf", &old_bytesps) != 1) {
    old_bytesps = 1600000000;
    multilog (log, LOG_WARNING, "header with no BYTES_PER_SECOND, using %lf\n", old_bytesps);
  }

  if (ascii_header_get (client->header, "FILE_SIZE", "%"PRIu64, &old_filesize) != 1) {
    multilog (log, LOG_INFO, "header with no FILE_SIZE, ignoring param\n");
  }

  if (ctx->verbose)
    multilog (log, LOG_INFO, "parsed old BANDWIDTH=%lf, CFREQ=%lf, TSAMP=%lf, BYTES_PER_SECOND=%lf\n",
              old_bw, old_cf, old_tsamp, old_bytesps);

  // generate the new BANDWIDTH, CFREQ and TSAMP
  new_bw = (old_bw / ctx->tdec) * powf(-1, ctx->nyquist_zone);
  new_cf = old_cf + old_bw * ((1.0 / ctx->tdec) * (0.5 + ctx->nyquist_zone) - 0.5);
  new_tsamp = old_tsamp * ctx->tdec;
  new_bytesps = old_bytesps / ctx->tdec;
  new_filesize = old_filesize / ctx->tdec;
  
  if (ctx->verbose)
    multilog (log, LOG_INFO, "setting new BANDWIDTH=%lf, CFREQ=%lf, TSAMP=%lf, BYTES_PER_SECOND=%lf\n",
              new_bw, new_cf, new_tsamp, new_bytesps);

  // get the header from the input data block
  uint64_t header_size = ipcbuf_get_bufsz (client->header_block);

  assert( header_size == ipcbuf_get_bufsz (ctx->hdu->header_block) );

  // get the next free header block on the out HDU
  header = ipcbuf_get_next_write (ctx->hdu->header_block);
  if (!header)  {
    multilog (log, LOG_ERR, "could not get next header block\n");
    return -1;
  }

  // copy the header from the in to the out
  memcpy ( header, client->header, header_size );

  if (ascii_header_set (header, "CFREQ", "%lf", new_cf) < 0) {
    multilog (log, LOG_WARNING, "failed to set CFREQ in outgoing header\n");
  }
  if (ascii_header_set (header, "FREQ", "%lf", new_cf) < 0) {
    multilog (log, LOG_WARNING, "failed to set FREQ in outgoing header\n");
  }
  if (ascii_header_set (header, "BANDWIDTH", "%lf", new_bw) < 0) {
    multilog (log, LOG_WARNING, "failed to set BANDWIDTH in outgoing header\n");
  }
  if (ascii_header_set (header, "BW", "%lf", new_bw) < 0) {
    multilog (log, LOG_WARNING, "failed to set BW in outgoing header\n");
  }
  if (ascii_header_set (header, "TSAMP", "%lf", new_tsamp) < 0) {
    multilog (log, LOG_WARNING, "failed to set TSAMP in outgoing header\n");
  }
  if (ascii_header_set (header, "BYTES_PER_SECOND", "%lf", new_bytesps) < 0) {
    multilog (log, LOG_WARNING, "failed to set BYTES_PER_SECOND in outgoing header\n");
  }
  if (old_filesize)
    if (ascii_header_set (header, "FILE_SIZE", "%"PRIu64, new_filesize) < 0) {
    multilog (log, LOG_WARNING, "failed to set FILE_SIZE in outgoing header\n");
  }

  // mark the outgoing header as filled
  if (ipcbuf_mark_filled (ctx->hdu->header_block, header_size) < 0)  {
    multilog (log, LOG_ERR, "Could not mark filled Header Block\n");
    return EXIT_FAILURE;
  }

  if (ctx->verbose) 
    multilog (log, LOG_INFO, "HDU (key=%x) opened for writing\n", ctx->key);

  client->transfer_bytes = 0; 
  client->optimal_bytes = 64*1024*1024 * ctx->tdec;

  if (ctx->verbose)
    multilog (log, LOG_INFO, "bytes_in=%"PRIu64", bytes_out=%"PRIu64"\n",
                    ctx->bytes_in, ctx->bytes_out );

  ctx->idec = ctx->tdec;
  
  client->header_transfer = 0;
  return 0;
}

/*! Function that closes the data transfer */
int dbdecidb_close (dada_client_t* client, uint64_t bytes_written)
{
  // the caspsr_dbdecidb specific data
  caspsr_dbdecidb_t* ctx = 0;

  // status and error logging facility
  multilog_t* log;

  assert (client != 0);

  ctx = (caspsr_dbdecidb_t*) client->context;

  assert (ctx != 0);
  assert (ctx->hdu != 0);

  log = client->log;
  assert (log != 0);

  if (ctx->block_open)
  {
    if (ipcio_close_block_write (ctx->hdu->data_block, ctx->bytes_written) < 0)
    {
      multilog (log, LOG_ERR, "dbdecidb_close: ipcio_close_block_write failed\n");
      return -1;
    }
    ctx->block_open = 0;
    ctx->outdat = 0;
    ctx->bytes_written = 0;
  }


  if (dada_hdu_unlock_write (ctx->hdu) < 0)
  {
    multilog (log, LOG_ERR, "dbdecidb_close: cannot unlock DADA HDU (key=%x)\n", ctx->key);
    return -1;
  }

  return 0;
}

/*! Pointer to the function that transfers data to/from the target */
int64_t dbdecidb_write (dada_client_t* client, void* data, uint64_t data_size)
{

  // the caspsr_dbdecidb specific data
  caspsr_dbdecidb_t* ctx = 0;

  // status and error logging facility
  multilog_t * log = 0;
  
  assert (client != 0);
  ctx = (caspsr_dbdecidb_t*) client->context;

  assert (client->log != 0);
  log = client->log;

  assert (ctx != 0);
  assert (ctx->hdu != 0);

  // decimate the data down, making just a few assumptions about it :)
  char * d = (char *) data;

  uint64_t indat = 0;
  uint64_t outdat = 0;
  uint64_t deci_data_size = data_size / ctx->tdec;

  unsigned idec = 0;

  for (indat=0; indat < data_size; indat++)
  {
    idec = idec % ctx->tdec;

    if (indat && (indat % 4 == 0))
      indat += 4;

    if (idec == 0)
    {
      if (outdat && (outdat % 4 == 0))
        outdat += 4;

      d[outdat] = d[indat];
      d[outdat+4] = d[indat+4];
      outdat++;
    }
    idec++; 
  }

  // write data to the current queue
  int64_t bytes_written = ipcio_write (ctx->hdu->data_block, data, deci_data_size );

  ctx->bytes_in += data_size;
  ctx->bytes_out += deci_data_size;

  if (ctx->verbose)
    multilog (log, LOG_INFO, "dbdecidb_write: read %"PRIu64", wrote %"PRIu64" bytes\n", data_size, deci_data_size);
 
  return data_size;
}

/*! Pointer to the function that transfers data to/from the target */
int64_t dbdecidb_write_block (dada_client_t* client, void* data, uint64_t data_size, 
                              uint64_t block_id)
{
 // the caspsr_dbdecidb specific data
  caspsr_dbdecidb_t* ctx = 0;

  // status and error logging facility
  multilog_t * log = 0;

  assert (client != 0);
  ctx = (caspsr_dbdecidb_t*) client->context;

  assert (client->log != 0);
  log = client->log;

  assert (ctx != 0);
  assert (ctx->hdu != 0);

  if (ctx->verbose > 1)
    multilog (log, LOG_INFO, "dbdecidb_write_block: write %"PRIu64" bytes, block=%"PRIu64"\n",
                data_size, block_id);

  char * d = (char *) data;
  uint64_t out_block_id = 0;

  // open the next block on the outgoing HDU
  uint64_t indat = 0;
  uint64_t deci_data_size = data_size / ctx->tdec;

  uint64_t i = 0;
  uint64_t ndat = data_size / (4 * 2);
  uint64_t idat = 0;

  char * in1 =  (char *) data;
  char * in2 =  (char *) data + 4;

  char * ou1 = 0;
  char * ou2 = 0;

  if (ctx->block_open)
  {
    ou1 = ctx->curr_block;
    ou2 = ctx->curr_block+4;
  }

  unsigned icnt = 0;

  uint64_t ndat_per_block = ctx->out_block_size / 2;

  for (idat=0; idat<data_size; idat++)
  {

    // if this is the sample we wish to use
    if (ctx->idec == ctx->tdec)
    {
      // if we need a new output block to write to
      if (!ctx->block_open)
      {
        if (ctx->verbose > 1)
          multilog (log, LOG_INFO, "dbdecidb_write_block: ipcio_open_block_write()\n");

        ctx->curr_block = ipcio_open_block_write(ctx->hdu->data_block, &out_block_id);
        if (!ctx->curr_block)
        {
          multilog (log, LOG_ERR, "dbdecidb_write_block: ipcio_open_block_write error %s\n", strerror(errno));
          return -1;
        }

        ctx->block_open = 1;
        ou1 = ctx->curr_block;
        ou2 = ctx->curr_block + 4;
      }

      ou1[ctx->outdat] = in1[idat];    // pol1
      ou2[ctx->outdat] = in2[idat];    // pol2
      ctx->idec = 0;
      ctx->outdat++;
      ctx->bytes_written += 2;

      ctx->ocnt++;
      if (ctx->ocnt == 4)
      {
        ctx->outdat += 4;
        ctx->ocnt = 0;
      }

       // check if the output block is now full
      if (ctx->bytes_written == ctx->out_block_size)
      {
        if (ctx->verbose > 1)
          multilog (log, LOG_INFO, "dbdecidb_write_block: close_block_write(%"PRIu64"\n", ctx->bytes_written);
        if (ipcio_close_block_write (ctx->hdu->data_block, ctx->bytes_written) < 0)
        {
          multilog (log, LOG_ERR, "dbdecidb_write_block: ipcio_close_block_write failed\n");
          return -1;
        }
        ctx->block_open = 0;
        ctx->outdat = 0;
        ctx->bytes_written = 0;
      }
    }

    ctx->idec++;

    // if we have reached the input stride incrememnt
    icnt++;
    if (icnt == 4)
    {
      idat += 4;
      icnt = 0;
    }

  }
#if 0
  for (indat=0; indat < data_size; indat++)
  {

    // if we need a new output block to write to
    if (!ctx->block_open) 
    {
      if (ctx->verbose > 1)
        multilog (log, LOG_INFO, "ipcio_open_block_write()\n");
      ctx->curr_block = ipcio_open_block_write(ctx->hdu->data_block, &out_block_id);
      if (!ctx->curr_block) 
      {
        multilog (log, LOG_ERR, "dbdecidb_write_block: ipcio_open_block_write error %s\n", strerror(errno));
        return -1;
      }
      ctx->block_open = 1;
    }

    idec = idec % ctx->tdec;

    if (indat && (indat % 4 == 0))
      indat += 4;

    if (idec == 0)
    {
      if (ctx->outdat && (ctx->outdat % 4 == 0))
        ctx->outdat += 4;

      ctx->curr_block[ctx->outdat] = d[indat];
      ctx->curr_block[ctx->outdat+4] = d[indat+4];
      ctx->outdat++;
      ctx->bytes_written += 2;
    }
    idec++;

    // check if the output block is now full
    if (ctx->bytes_written == ctx->out_block_size)
    {
      if (ctx->verbose > 1)
        multilog (log, LOG_INFO, "bytes_written=%"PRIu64", block_size=%"PRIu64"\n", 
            ctx->bytes_written, ctx->out_block_size);
      if (ipcio_close_block_write (ctx->hdu->data_block, ctx->bytes_written) < 0)
      {
        multilog (log, LOG_ERR, "dbdecidb_write_block: ipcio_close_block_write failed\n");
        return -1;
      }
      ctx->block_open = 0;
      ctx->outdat = 0;
      ctx->bytes_written = 0;
    }
  }
#endif

  ctx->bytes_in += data_size;
  ctx->bytes_out += deci_data_size;

  if (ctx->verbose > 1)
    multilog (log, LOG_INFO, "dbdecidb_write_block: read %"PRIu64", wrote %"PRIu64" bytes\n", data_size, deci_data_size);

  return data_size;


}



int main (int argc, char **argv)
{
  /* DADA Data Block to Disk configuration */
  caspsr_dbdecidb_t dbdecidb = CASPSR_DBDECIDB_INIT;

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

  // decimation factor
  unsigned tdec = 2;

  // number of transfers
  unsigned single_transfer = 1;

  // use zero copy transfers
  unsigned zero_copy = 0;

  // input data block HDU key
  key_t in_key = 0;

  int arg = 0;

  while ((arg=getopt(argc,argv,"dn:st:vz")) != -1)
  {
    switch (arg) 
    {
      
      case 'd':
        daemon = 1;
        break;

      case 's':
        single_transfer = 1;
        break;

      case 'n':
        if (optarg)
        {
          dbdecidb.nyquist_zone = atoi(optarg);
          break;
        }
        else
        {
          fprintf (stderr, "caspsr_dbdecidb: -n requires argument\n");
          usage();
          return EXIT_FAILURE;
        }

      case 't':
        if (!optarg)
        { 
          fprintf (stderr, "caspsr_dbdecidb: -t requires argument\n");
          usage();
          return EXIT_FAILURE;
        }
        if (sscanf (optarg, "%d", &tdec) != 1) 
        {
          fprintf (stderr ,"caspsr_dbdecidb: could not parse tdec from %s\n", optarg);
          usage();
          return EXIT_FAILURE;
        } 
        break;
        
      case 'v':
        verbose++;
        break;
        
      case 'z':
        zero_copy = 1;
        break;
        
      default:
        usage ();
        return 0;
      
    }
  }

  dbdecidb.verbose = verbose;
  dbdecidb.tdec = tdec;

  int num_args = argc-optind;
  int i = 0;
      
  if (num_args != 2)
  {
    fprintf(stderr, "caspsr_dbdecidb: must specify 2 datablocks\n");
    usage();
    exit(EXIT_FAILURE);
  } 

  if (verbose)
    fprintf (stderr, "parsing input key=%s\n", argv[optind]);
  if (sscanf (argv[optind], "%x", &in_key) != 1) {
    fprintf (stderr, "caspsr_dbdecidb: could not parse in key from %s\n", argv[optind]);
    return EXIT_FAILURE;
  }

  if (verbose)
    fprintf (stderr, "parsing output key=%s\n", argv[optind+1]);
  if (sscanf (argv[optind+1], "%x", &(dbdecidb.key)) != 1) {
    fprintf (stderr, "caspsr_dbdecidb: could not parse out key from %s\n", argv[optind+1]);
    return EXIT_FAILURE;
  }

  log = multilog_open ("caspsr_dbdecidb", 0);

  multilog_add (log, stderr);

  if (verbose)
    multilog (log, LOG_INFO, "main: creating in hdu\n");

  // open connection to the in/read DB
  hdu = dada_hdu_create (log);

  dada_hdu_set_key (hdu, in_key);

  if (dada_hdu_connect (hdu) < 0)
    return EXIT_FAILURE;

  if (verbose)
    multilog (log, LOG_INFO, "main: lock read key=%x\n", in_key);

  if (dada_hdu_lock_read (hdu) < 0)
    return EXIT_FAILURE;


  // open connection to the out/write DB
  dbdecidb.hdu = dada_hdu_create (log);
  
  // set the DADA HDU key
  dada_hdu_set_key (dbdecidb.hdu, dbdecidb.key);
  
  // connect to the out HDU
  if (dada_hdu_connect (dbdecidb.hdu) < 0)
  {
    multilog (log, LOG_ERR, "cannot connected to DADA HDU (key=%x)\n", dbdecidb.key);
    return -1;
  } 
  
  // for zerocopy, input and output datablocks must have the same block size
  if (zero_copy)
  {
    uint64_t in_block_size = ipcbuf_get_bufsz ( (ipcbuf_t *) hdu->data_block);
    dbdecidb.out_block_size = ipcbuf_get_bufsz ( (ipcbuf_t *) dbdecidb.hdu->data_block);

    if (verbose)
      multilog (log, LOG_INFO, "main: in_block_size=%"PRIu64", out_block_size=%"PRIu64"\n", in_block_size, dbdecidb.out_block_size);

    /*
    if (in_block_size != out_block_size)
    {
      multilog (log, LOG_ERR, "for zero copy, input and output block sizes must be the "
                "same. in=%"PRIu64" != out=%"PRIu64"\n", in_block_size, out_block_size);

      dada_hdu_disconnect (hdu);
      dada_hdu_disconnect (dbdecidb.hdu);
      return EXIT_FAILURE;
    }*/
  }

  client = dada_client_create ();

  client->log = log;

  client->data_block   = hdu->data_block;
  client->header_block = hdu->header_block;

  client->open_function  = dbdecidb_open;
  client->io_function    = dbdecidb_write;

  if (zero_copy)
    client->io_block_function = dbdecidb_write_block;

  client->close_function = dbdecidb_close;
  client->direction      = dada_client_reader;

  client->context = &dbdecidb;

  if (dada_client_read (client) < 0)
    multilog (log, LOG_ERR, "Error during transfer\n");

  if (dada_hdu_unlock_read (hdu) < 0)
    return EXIT_FAILURE;

  if (dada_hdu_disconnect (hdu) < 0)
    return EXIT_FAILURE;

  return EXIT_SUCCESS;
}


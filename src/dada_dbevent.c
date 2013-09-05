/***************************************************************************
 *  
 *    Copyright (C) 2012 by Andrew Jameson
 *    Licensed under the Academic Free License version 2.1
 * 
 ****************************************************************************/

/*
 * Attaches to in input data block as a viewer, and opens a socket to listen
 * for requests to write temporal events to the output data block. Can seek
 * back in time over cleared data blocks
 */

#include "dada_hdu.h"
#include "dada_def.h"
#include "node_array.h"
#include "multilog.h"
#include "diff_time.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <signal.h>

#include <sys/types.h>
#include <sys/socket.h>

#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>

#define IPCBUF_EODACK 3   /* acknowledgement of end of data */
#define DADA_DBEVENT_DEFAULT_PORT 30000
#define DADA_DBEVENT_DEFAULT_INPUT_BUFFER 80
#define DADA_DBEVENT_DEFAULT_INPUT_DELAY 60
#define DADA_DBEVENT_TIMESTR "%Y-%m-%d-%H:%M:%S"

int quit = 0;

typedef struct {

  // input HDU
  dada_hdu_t * in_hdu;  

  // output HDU
  dada_hdu_t * out_hdu;

  // multilog 
  multilog_t * log;

  // input data block's UTC_START
  time_t utc_start;

  // input data block's BYTES_PER_SECOND
  uint64_t bytes_per_second;

  time_t input_maximum_delay;

  void * work_buffer;

  size_t work_buffer_size;

  uint64_t curr_write_buf;

  uint64_t prev_write_buf;

  uint64_t curr_read_buf;

  time_t * write_times;

  uint64_t in_nbufs;

  uint64_t in_bufsz;

  // verbosity
  int verbose;

} dada_dbevent_t;

typedef struct {

  uint64_t start_byte;

  uint64_t end_byte;

  float snr;

  float dm;

} event_t;

#define DADA_DBEVENT_INIT { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }

static int sort_events (const void *p1, const void *p2)
{
  event_t A = *(event_t *) p1;
  event_t B = *(event_t *) p2;
  if (A.start_byte < B.start_byte) return -1;
  if (A.start_byte > B.start_byte) return +1;
  return 0;
}

int check_write_timestamps(dada_dbevent_t * dbevent);

int64_t calculate_byte_offset (dada_dbevent_t * dbevent, char * time_str_secs, char * time_str_frac);

int dump_event(dada_dbevent_t * dbevent, double event_start_utc, double event_end_utc, float event_snr, float event_dm);

void usage();

void usage()
{
  fprintf (stdout,
     "dada_dbevent [options] inkey outkey\n"
     " inkey      input hexadecimal shared memory key\n"
     " outkey     input hexadecimal shared memory key\n"
     " -b percent madelay procesing of the input buffer up to this amount [default %d %]\n"
     " -t delay   maximum delay (s) to retain data for [default %ds]\n"
     " -h         print this help text\n"
     " -p port    port to listen for event commands [default %d]\n"
     " -v         be verbose\n", 
     DADA_DBEVENT_DEFAULT_INPUT_BUFFER, 
     DADA_DBEVENT_DEFAULT_INPUT_DELAY, 
     DADA_DBEVENT_DEFAULT_PORT);
}

void signal_handler(int signalValue) 
{
  fprintf(stderr, "dada_dbevent: SIGINT/TERM\n");
  quit = 1;
}

int main (int argc, char **argv)
{
  // core dbevent data struct
  dada_dbevent_t dbevent = DADA_DBEVENT_INIT;

  // DADA Logger
  multilog_t* log = 0;

  // flag set in verbose mode
  char verbose = 0;

  // port to listen for event requests
  int port = DADA_DBEVENT_DEFAULT_PORT;

  // input hexadecimal shared memory key
  key_t in_dada_key;

  // output hexadecimal shared memory key
  key_t out_dada_key;

  char daemon = 0;

  float input_data_block_threshold = DADA_DBEVENT_DEFAULT_INPUT_BUFFER;

  time_t input_maximum_delay = DADA_DBEVENT_DEFAULT_INPUT_DELAY;

  int arg = 0;

  while ((arg=getopt(argc,argv,"b:hp:t:v")) != -1)
  {
    switch (arg)
    {
      case 'b':
        if (sscanf (optarg, "%f", &input_data_block_threshold) != 1)
        {
          fprintf (stderr, "dada_dbevent: could not parse input buffer level from %s\n", optarg);
          return EXIT_FAILURE;
        }
        break;

      case 'h':
        usage();
        return EXIT_SUCCESS;

      case 'v':
        verbose++;
        break;

      case 'p':
        if (sscanf (optarg, "%d", &port) != 1) 
        {
          fprintf (stderr, "dada_dbevent: could not parse port from %s\n", optarg);
          return EXIT_FAILURE;
        }
        break;

      case 't':
        if (sscanf (optarg, "%d", &input_maximum_delay) != 1)
        {
          fprintf (stderr, "dada_dbevent: could not parse maximum input delay from %s\n", optarg);
          return EXIT_FAILURE;
        }
        break;

      default:
        usage ();
        return EXIT_SUCCESS;
    }
  }

  if (argc - optind != 2)
  { 
    fprintf (stderr, "dada_dbevent: expected 2 command line arguments\n");
    usage();
    return EXIT_FAILURE;
  }

  if (sscanf (argv[optind], "%x", &in_dada_key) != 1) 
  {
    fprintf (stderr,"dada_dbevent: could not parse in_key from %s\n", argv[optind]);
    return EXIT_FAILURE;
  }

  if (sscanf (argv[optind+1], "%x", &out_dada_key) != 1) 
  {
    fprintf (stderr,"dada_dbevent: could not parse out_key from %s\n", argv[optind+1]);
    return EXIT_FAILURE;
  }

  // install some signal handlers
  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);

  log = multilog_open ("dada_dbevent", 0);
  multilog_add (log, stderr);

  dbevent.verbose = verbose;
  dbevent.log = log;
  dbevent.input_maximum_delay = input_maximum_delay;
  dbevent.work_buffer_size = 1024 * 1024; 
  dbevent.work_buffer = malloc (dbevent.work_buffer_size);
  if (!dbevent.work_buffer)
  {
    multilog(log, LOG_INFO, "could not allocate memory for work buffer\n");
    return EXIT_FAILURE;
  }

  if (verbose)
    multilog(log, LOG_INFO, "connecting to data blocks\n");

  dbevent.in_hdu = dada_hdu_create (log);
  dada_hdu_set_key (dbevent.in_hdu, in_dada_key);
  if (dada_hdu_connect (dbevent.in_hdu) < 0)
  {
    multilog(log, LOG_ERR, "could not connect to input HDU\n");
    return EXIT_FAILURE;
  }
  if (dada_hdu_lock_read(dbevent.in_hdu) < 0)
  {
    multilog (log, LOG_ERR, "could not open input HDU as viewer\n");
    return EXIT_FAILURE;
  }

  dbevent.out_hdu = dada_hdu_create (log);
  dada_hdu_set_key (dbevent.out_hdu, out_dada_key);
  if (dada_hdu_connect (dbevent.out_hdu) < 0)
  {
    multilog(log, LOG_ERR, "could not connect to output HDU\n");
    return EXIT_FAILURE;
  }
  if (dada_hdu_lock_write (dbevent.out_hdu) < 0)
  {
    multilog (log, LOG_ERR, "could not open output HDU as writer\n");
    return EXIT_FAILURE;
  }

  // open listening socket
  if (verbose)
    multilog (log, LOG_INFO, "main: sock_create(%d)\n", port);
  int listen_fd = sock_create (&port);
  if (listen_fd < 0)
  { 
    multilog (log, LOG_ERR, "could not open socket: %s\n", strerror(errno));
    quit = 2;
  }

  fd_set fds;
  struct timeval timeout;
  int fds_read;
  
  // now get the header from the input data block
  multilog(log, LOG_INFO, "waiting for input header\n");
  if (dada_hdu_open (dbevent.in_hdu) < 0)
  {
    multilog (log, LOG_ERR, "could not get input header\n");
    quit = 1;
  }
  else
  {
    if (verbose > 1)
    {
      fprintf (stderr, "==========\n");
      fprintf (stderr, "%s", dbevent.in_hdu->header);
      fprintf (stderr, "==========\n");
    }

    char utc_buffer[64];
    // get the UTC_START and TSAMP / BYTES_PER_SECOND for this observation    
    if (ascii_header_get (dbevent.in_hdu->header, "UTC_START", "%s", utc_buffer) < 0) 
    {
      multilog (log, LOG_ERR, "could not extract UTC_START from input datablock header\n");
      quit = 2;
    }
    else
    {
      if (verbose)
        multilog(log, LOG_INFO, "input UTC_START=%s\n", utc_buffer);
      dbevent.utc_start = str2utctime (utc_buffer);
      if (dbevent.utc_start == (time_t)-1) 
      {
        multilog (log, LOG_ERR, "could not parse UTC_START from '%s'\n", utc_buffer);
        quit = 2;
      }
    }

    if (ascii_header_get (dbevent.in_hdu->header, "BYTES_PER_SECOND", "%"PRIu64, &(dbevent.bytes_per_second)) < 0) 
    {
      multilog (log, LOG_ERR, "could not extract BYTES_PER_SECOND from input datablock header\n");
      quit = 2;
    }
    else
    {
      if (verbose)
        multilog(log, LOG_INFO, "input BYTES_PER_SECOND=%"PRIu64"\n", dbevent.bytes_per_second);
    }
  }

  uint64_t ibuf = 0;

  ipcbuf_t * db = (ipcbuf_t *) dbevent.in_hdu->data_block;

  // get the number and size of buffers in the input data block
  dbevent.in_nbufs = ipcbuf_get_nbufs (db);
  dbevent.in_bufsz = ipcbuf_get_bufsz (db);
  dbevent.write_times = (time_t *) malloc (sizeof(time_t) * dbevent.in_nbufs);
  for (ibuf=0; ibuf < dbevent.in_nbufs; ibuf++)
    dbevent.write_times[ibuf] = 0;

  dbevent.curr_write_buf = ipcbuf_get_write_count (db);
  dbevent.prev_write_buf = ipcbuf_get_read_count (db);
  dbevent.curr_read_buf  = 0;

  //uint64_t read_buf = 0;
  //time_t read_time = 0;
  //time_t read_time_diff = 0;
  //unsigned time_ok = 0;

  while ( ! quit )
  {
    // setup file descriptor set for listening
    FD_ZERO(&fds);
    FD_SET(listen_fd, &fds);
    timeout.tv_sec = 0;
    timeout.tv_usec = 100000;
    fds_read = select(listen_fd+1, &fds, (fd_set *) 0, (fd_set *) 0, &timeout);

    // problem with select call
    if (fds_read < 0)
    {
      multilog (log, LOG_ERR, "select failed: %s\n", strerror(errno));
      quit = 2;
      break;
    }
    // select timed out, check input HDU for end of data
    else if (fds_read == 0)
    {
      multilog (log, LOG_INFO, "main: check_write_timestamps()\n");
      int64_t n_skipped = check_write_timestamps (&dbevent);
      if (n_skipped < 0)
        multilog (log, LOG_WARNING, "check_db_times failed\n");
      multilog (log, LOG_INFO, "main: check_db_times skipped %"PRIi64" events\n", n_skipped);
    }
    // we received a new connection on our listening FD, process comand
    else
    {
      multilog (log, LOG_INFO, "main: receiving events on socket\n");
      int events_recorded = receive_events (&dbevent, listen_fd);
      multilog (log, LOG_INFO, "main: received %d events\n", events_recorded);
    }

    // check how full the input datablock is
    float percent_full = ipcio_percent_full (dbevent.in_hdu->data_block) * 100;
    if (verbose > 1)
      multilog (log, LOG_INFO, "input datablock %5.2f percent full\n", percent_full);

    uint64_t bytes = 0;
    uint64_t block_id = 0;
    while (!quit && percent_full > input_data_block_threshold)
    {
      ipcio_open_block_read (dbevent.in_hdu->data_block, &bytes, &block_id);
      if (verbose > 1)
        multilog (log, LOG_INFO, "ipcio reading block %"PRIu64" of size %"PRIu64"\n", block_id, bytes);
      ipcio_close_block_read (dbevent.in_hdu->data_block, bytes);
      percent_full = ipcio_percent_full (dbevent.in_hdu->data_block) * 100;
      if (verbose > 1)
        multilog (log, LOG_INFO, "input datablock reduced to %5.2f percent full\n", percent_full);
    }

    if (ipcbuf_eod (db))
    {
      multilog (log, LOG_INFO, "EOD now true\n");
      quit = 1;
    }

  }

  free (dbevent.write_times);
  free (dbevent.work_buffer);

  if (dada_hdu_disconnect (dbevent.in_hdu) < 0)
  {
    fprintf (stderr, "dada_dbevent: disconnect from input data block failed\n");
    return EXIT_FAILURE;
  }

  if (dada_hdu_unlock_write (dbevent.out_hdu) < 0)
  {
    fprintf (stderr, "dada_dbevent: unlock write on output data block failed\n");
    return EXIT_FAILURE;
  }
  if (dada_hdu_disconnect (dbevent.out_hdu) < 0)
  {
    fprintf (stderr, "dada_dbevent: disconnect from output data block failed\n");
    return EXIT_FAILURE;
  }

  fprintf (stderr, "dada_dbevent: DONE :)\n");
  return EXIT_SUCCESS;
}


int check_write_timestamps (dada_dbevent_t * dbevent)
{
  multilog_t * log = dbevent->log;

  uint64_t buf;

  // flag to check there are no old buffers left
  unsigned old_buffers = 1;

  int64_t n_skipped = 0;

  // get the most recently written buffer 
  dbevent->curr_write_buf = ipcbuf_get_write_count ((ipcbuf_t *) dbevent->in_hdu->data_block);

  if (dbevent->verbose > 1)
    multilog (log, LOG_INFO, "check_write_timestamps: curr_write_buf=%"PRIu64"\n", dbevent->curr_write_buf);

  // update all write times for bufs written since the last call to this function
  while (dbevent->prev_write_buf < dbevent->curr_write_buf)
  {
    buf = dbevent->prev_write_buf % dbevent->in_nbufs;
    dbevent->write_times[buf] = time(0);
    if (dbevent->verbose > 1)
      multilog (log, LOG_INFO, "check_write_timestamps: write_times[%"PRIu64"] = %d\n", 
                buf, dbevent->write_times[buf]);
    dbevent->prev_write_buf ++; 
  }
 
  time_t read_time;
  time_t read_time_diff;
 
  while (old_buffers > 0)
  {
      // now determine the current read buffer
    dbevent->curr_read_buf = ipcbuf_get_read_count ((ipcbuf_t *) dbevent->in_hdu->data_block);
    buf = dbevent->curr_read_buf % dbevent->in_nbufs;

    read_time = dbevent->write_times[buf];
    read_time_diff = time(0) - read_time;
   
    if (dbevent->verbose > 1)
      multilog (log, LOG_INFO, "check_write_timestamps: read_buf=%"PRIu64", time=%d, diff=%d\n",
                  dbevent->curr_read_buf, read_time, read_time_diff);
 
    // if the current write buffer is greater than our current read buffer and the read buffers input delay
    // exceeds the maximum, then we wil read it and check the next one
    if (read_time && (dbevent->curr_write_buf > dbevent->curr_read_buf) && (read_time_diff > dbevent->input_maximum_delay))
    {
      uint64_t read_byte = ipcio_tell (dbevent->in_hdu->data_block);
      uint64_t remainder = read_byte % dbevent->in_bufsz;
      uint64_t seek_byte = 0;

      // check for a partially read block
      if (remainder != 0)
      {
        seek_byte = dbevent->in_bufsz - remainder;
      }
      // otherwise we will seek forward 1 full block
      else
      {
        seek_byte = dbevent->in_bufsz;
      }

      multilog (dbevent->log, LOG_INFO, "check_write_timestamps: ipcio_seek(%"PRIu64", SEEK_CUR)\n", seek_byte);
      int64_t seeked_byte = ipcio_seek (dbevent->in_hdu->data_block, seek_byte, SEEK_CUR);
      if (seeked_byte < 0)
      {
        multilog (dbevent->log, LOG_INFO, "check_write_timestamps: ipcio_seek failed\n");
        return -1;
      }
      n_skipped ++;
    }
    // the current read buffer is not too old, drop out of this loop
    else
      old_buffers = 0;

    // also check for end of data
    if (ipcbuf_eod ((ipcbuf_t *) dbevent->in_hdu->data_block))
    {
      old_buffers = 1;
    }
  }
  if (dbevent->verbose)
    multilog (dbevent->log, LOG_INFO, "check_write_timestamps: skipped %"PRIi64" blocks\n", n_skipped);
  return n_skipped;
}

int receive_events (dada_dbevent_t * dbevent, int listen_fd)
{
  multilog_t * log = dbevent->log;

  int    fd     = 0;
  FILE * sockin = 0;
  char * rgot   = 0;
  unsigned more_events = 1;

  unsigned buffer_size = 1024;
  char buffer[buffer_size];

  char * event_start;
  char * event_start_fractional;
  char * event_end;
  char * event_end_fractional;
  char * event_snr_str;
  char * event_dm_str;

  time_t   event_time_secs;
  uint64_t event_time_fractional;
  double   event_time;

  // arrays for n_events
  uint64_t  n_events = 0;
  event_t * events;

  uint64_t events_recorded = 0;
  uint64_t events_missed = 0;

  if (dbevent->verbose)
    multilog (log, LOG_INFO, "main: sock_accept(listen_fd)\n");
  fd = sock_accept (listen_fd);
  if (fd < 0)
  {
    multilog(log, LOG_WARNING, "error accepting connection %s\n", strerror(errno));
    return -1;
  }

  sockin = fdopen(fd,"r");
  if (!sockin)
  {
    multilog(log, LOG_WARNING, "error creating input stream %s\n", strerror(errno));
    close(fd);
    return -1;
  }

  setbuf (sockin, 0);

  // first line on the socket should be the number of events
  rgot = fgets (buffer, buffer_size, sockin);
  if (sscanf (buffer, "N_EVENTS %"PRIu64, &n_events) != 1)
  {
    multilog(log, LOG_WARNING, "failed to parse N_EVENTS\n");
    more_events = 0;  
  }
  else
  {
    events = (event_t *) malloc (sizeof(event_t) * n_events);
  }

  char * comment = 0;
  unsigned i = 0;
  const char * sep = ". \t";
  char * word;
  
  while (more_events && !feof(sockin))
  {
    multilog (log, LOG_INFO, "getting new line\n");
    char * saveptr = 0;

    rgot = fgets (buffer, buffer_size, sockin);

    if (dbevent->verbose > 1)
       multilog (log, LOG_INFO, " <- %s\n", buffer);

    // ignore comments
    comment = strchr( buffer, '#' );
    if (comment)
      *comment = '\0';

    comment = strchr( buffer, '\r' );
    if (comment)
      *comment = '\0';

    if (dbevent->verbose)
       multilog (log, LOG_INFO, "< - %s\n", buffer);

    if (strlen(buffer) < 10)
      continue;

    // extract START_UTC string excluding sub-second components
    event_start = strtok_r (buffer, sep, &saveptr);
    if (event_start == NULL)
    {
      multilog (log, LOG_WARNING, "receive_events: problem extracting event_start\n");
      more_events = 0;
      continue;
    }
    event_start_fractional = strtok_r (NULL, sep, &saveptr);

    events[i].start_byte = calculate_byte_offset (dbevent, event_start, event_start_fractional);
    //event_start_offset[i] = calculate_byte_offset (dbevent, event_start, event_start_fractional);

    // extract END_UTC string excluding sub-second components
    event_end = strtok_r (NULL, sep, &saveptr);
    if (event_end == NULL)
    {
      multilog (log, LOG_WARNING, "receive_events: problem extracting event_end\n");
      more_events = 0;
      continue;
    }
    event_end_fractional = strtok_r (NULL, sep, &saveptr);
    events[i].end_byte = calculate_byte_offset (dbevent, event_end, event_end_fractional);

    event_snr_str = strtok_r (NULL, sep, &saveptr);
    sscanf(event_snr_str, "%f", &(events[i].snr));

    event_dm_str = strtok_r (NULL, sep, &saveptr);
    sscanf(event_dm_str, "%f", &(events[i].dm));

    multilog (dbevent->log, LOG_INFO, "event: %"PRIi64" - %"PRIi64" SNR=%f, DM=%f\n",
              events[i].start_byte , events[i].end_byte, events[i].snr, events[i].dm);

    i++;

    if (i >= n_events)
      more_events = 0;
  }

  // sort the events based on event start time
  qsort (events, n_events, sizeof (event_t), sort_events);

  // for each event, check that its in the future, and if so, seek forward to it
  uint64_t current_byte = 0;
  int64_t seeked_byte = 0;
  for (i=0; i<n_events; i++)
  {
    current_byte = ipcio_tell (dbevent->in_hdu->data_block);
    multilog (dbevent->log, LOG_INFO, "current_byte=%"PRIu64"\n", current_byte);

    if (events[i].start_byte < current_byte)
    {
      multilog (dbevent->log, LOG_WARNING, "skipping events[%d], past start_byte\n");
      events_missed++;
      continue;
    }

    // seek forward to the relevant point in the datablock
    multilog (dbevent->log, LOG_INFO, "seeking forward %"PRIu64" bytes from start of obs\n", events[i].start_byte);
    seeked_byte = ipcio_seek (dbevent->in_hdu->data_block, (int64_t) events[i].start_byte, SEEK_SET);
    if (seeked_byte < 0)
    {
      multilog (dbevent->log, LOG_WARNING, "could not seek to byte %"PRIu64"\n", events[i].start_byte);
      events_missed++;
      continue;
    }

    multilog (dbevent->log, LOG_INFO, "seeked_byte=%"PRIi64"\n", seeked_byte);

    // determine how much to read
    size_t to_read = events[i].end_byte - events[i].start_byte;
    multilog (dbevent->log, LOG_INFO, "to read = %d [%"PRIu64" - %"PRIu64"]\n", to_read, events[i].end_byte, events[i].start_byte);

    if (dbevent->work_buffer_size < to_read)
    {
      dbevent->work_buffer_size = to_read;
      multilog (dbevent->log, LOG_INFO, "reallocating work_buffer [%p] to %d bytes\n", 
                dbevent->work_buffer, dbevent->work_buffer_size);
      dbevent->work_buffer = realloc (dbevent->work_buffer, dbevent->work_buffer_size);
      multilog (dbevent->log, LOG_INFO, "reallocated work_buffer [%p]\n", dbevent->work_buffer);
    }
     
    // read the event from the input buffer 
    multilog (dbevent->log, LOG_INFO, "reading %d bytes from input HDU into work buffer\n", to_read);
    ssize_t bytes_read = ipcio_read (dbevent->in_hdu->data_block, dbevent->work_buffer, to_read);
    multilog (dbevent->log, LOG_INFO, "read %d bytes from input HDU into work buffer\n", bytes_read);
    if (bytes_read < 0)
    {
      multilog (dbevent->log, LOG_WARNING, "receive_events: ipcio_read on input HDU failed\n");
      return -1;
    }

    events_recorded++;

    // write the event to the output buffer
    char * header = ipcbuf_get_next_write (dbevent->out_hdu->header_block);
    uint64_t header_size = ipcbuf_get_bufsz (dbevent->out_hdu->header_block);

    // TODO improve header size stuff
    memcpy (header, dbevent->in_hdu->header, DADA_DEFAULT_HEADER_SIZE);

    // now write some relevant data to the header
    ascii_header_set (header, "EVENT_SNR", "%f", events[i].snr);
    ascii_header_set (header, "EVENT_DM", "%f",  events[i].dm);

    // tag this header as filled
    ipcbuf_mark_filled (dbevent->out_hdu->header_block, header_size);

    // write the specified amount to the output data block
    ipcio_write (dbevent->out_hdu->data_block, dbevent->work_buffer, to_read);

    // close the data block to ensure EOD is written
    if (dada_hdu_unlock_write (dbevent->out_hdu) < 0)
    {
      multilog (log, LOG_ERR, "could not close output HDU as writer\n");
      return EXIT_FAILURE; 
    }

    // lock write again to re-open for the next event
    if (dada_hdu_lock_write (dbevent->out_hdu) < 0)
    {
      multilog (log, LOG_ERR, "could not open output HDU as writer\n");
      return EXIT_FAILURE;
    }
  }

  multilog (dbevent->log, LOG_INFO, "events: recorded=%"PRIu64" missed=%"PRIu64"\n", events_recorded, events_missed);

  fclose(sockin);
  close (fd);

  return events_recorded;
}

int64_t calculate_byte_offset (dada_dbevent_t * dbevent, char * time_str_secs, char * time_str_frac)
{
  time_t   time_secs;         // integer time in seconds
  uint64_t time_frac_numer;   // numerator of fractional time
  uint64_t time_frac_denom;   // denominator of fractional time

  int64_t  event_byte_offset = -1;
  uint64_t event_byte;
  uint64_t event_byte_frac;
 
  time_secs = str2utctime (time_str_secs);
  sscanf (time_str_frac, "%"PRIu64, &time_frac_numer);
  time_frac_denom = 10 * strlen(time_str_frac);

  // check we have utc_start and that this event is in the future
  if (dbevent->utc_start && (time_secs >= dbevent->utc_start))
  {
    event_byte = (time_secs - dbevent->utc_start) * dbevent->bytes_per_second;
    event_byte_frac = time_frac_numer * dbevent->bytes_per_second;
    event_byte_frac /= time_frac_denom;
    event_byte_offset = (int64_t) event_byte + event_byte_frac;
  }
  return event_byte_offset;
}


/*
 * dump the specified event to the output datablock */
int dump_event(dada_dbevent_t * dbevent, double event_start_utc, double event_end_utc, float event_snr, float event_dm)
{
  multilog (dbevent->log, LOG_INFO, "event time: %lf - %lf [seconds]\n", event_start_utc, event_end_utc);
  multilog (dbevent->log, LOG_INFO, "event SNR: %f\n", event_snr);
  multilog (dbevent->log, LOG_INFO, "event DM: %f\n", event_dm);
}

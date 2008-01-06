#include "dada_client.h"
#include "dada_hdu.h"
#include "apsr_def.h"
#include "multilog.h"

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
#include <math.h>

#include <sys/types.h>
#include <sys/socket.h>

void generate_triripple(char *array, int array_size, int nbits);
int verify_tri_data(char * data, char * tri_data, uint64_t compare_size);
void print_triripple(char *array, int array_size, int nbits);

/* structures dbtriripple datatype  */
typedef struct {
  int verbose;                /* verbosity flag */
  int expect_header;          /* expect header */
  char *tri_data;             /* tri wave form data expected */
  uint64_t tri_data_length;   /* size of tri wave array */
  uint64_t tri_data_nbits;    /* nbits in tri wave array */
  uint64_t tri_data_counter;  /* tri wave array counter */
  multilog_t *log;
} dbtriripple_t;

void usage()
{
  fprintf (stdout,
     "dada_dbtriripple [options]\n"
     " -v         be verbose\n"
     " -l num     period of triangular wave form data [default 512]\n"
     " -b num     number of bits in triangular wave form data [default 8]\n"
     " -d         run as daemon\n\n"
     "Multilog output available on port %d\n",APSR_DEFAULT_DBTRIWAVE_LOG);
}


/*! Function that opens the data transfer target */
int dbtriripple_open_function (dada_client_t* client)
{

  dbtriripple_t *dbtriripple = client->context;
  dbtriripple->expect_header = 1;

  /* the header */
  char* header = 0;

  assert (client != 0);

  multilog_t *log = client->log;
  assert (log != 0);

  header = client->header;
  assert (header != 0);
  if (dbtriripple->verbose)
    multilog (log, LOG_INFO, "dbtriripple_open_function\n");

  sleep(20);

  return 0;
}

/*! Function that closes the data file */
int dbtriripple_close_function (dada_client_t* client, uint64_t bytes_written)
{

  /* status and error logging facility */
  multilog_t* log;

  dbtriripple_t *dbtriripple = client->context;

  assert (client != 0);

  log = client->log;
  assert (log != 0);

  if (dbtriripple->verbose)
    multilog (log, LOG_INFO, "dbtriripple_close_function\n");

#ifdef _DEBUG
  if (bytes_written < client->transfer_bytes) {
    multilog (log, LOG_INFO, "Transfer stopped early at %"PRIu64" bytes\n",
	      bytes_written);
  }
#endif

  return 0;
}

/*! Pointer to the function that transfers data to/from the target */
int64_t dbtriripple_io_function (dada_client_t* client, 
			    void* data, uint64_t data_size)
{

  /* keep track of the data as we loop through it... */
  uint64_t data_counter = 0;
  int retval = 0;
  uint64_t compare_size = 0;

  dbtriripple_t *dbtriripple = client->context;
  multilog_t *log = client->log;


  if (dbtriripple->verbose) 
    multilog (log, LOG_INFO, "dbtriripple_io_function: %"PRIu64"\n", data_size);

  /* If we are asked to read the header, just ignore and return */
  if (dbtriripple->expect_header) {
    if (dbtriripple->verbose) 
      multilog (log, LOG_INFO, "dbtriripple_io_function: read the header of "
                               "%"PRIu64" bytes\n", data_size);
    dbtriripple->expect_header = 0;
    data_counter = data_size;

  } else {

    /* If the tri wave is larger than the data block... */   
    if (dbtriripple->tri_data_length > data_size) {

      /* If the comparison would over run the data block, then do 2 compares */
      if (dbtriripple->tri_data_counter + data_size > dbtriripple->tri_data_length) 
      {

        /* compare up to the end of the tri_data block */
        compare_size = dbtriripple->tri_data_length - dbtriripple->tri_data_counter;
        
        retval = verify_tri_data(data, (dbtriripple->tri_data + 
                                 dbtriripple->tri_data_counter), compare_size);

        if (retval == -1) {
          multilog (log, LOG_ERR, "Error in the triripple data (1)\n");
        }
        
        uint64_t data_compared = compare_size;
        
        /* increment the counter */
        dbtriripple->tri_data_counter = 0;

        /* compare from the start of the tri_data block */
        compare_size = data_size - compare_size;
        retval = verify_tri_data(data+data_compared, dbtriripple->tri_data, 
                                 compare_size); 
        dbtriripple->tri_data_counter += compare_size;
        
        if (retval == -1) {
          multilog (log, LOG_ERR, "Error in the triripple data (2)\n");
        }
              
      } else {

        compare_size = data_size;
        retval = verify_tri_data(data, (dbtriripple->tri_data + 
                                 dbtriripple->tri_data_counter), compare_size);
        dbtriripple->tri_data_counter += compare_size;
      }

      /* we processed the memory block */
      data_counter = data_size;
    
    /* the data block is larger than the tri wave */  
    } else {

      while ((data_counter + dbtriripple->tri_data_length) <= data_size) {

        /* compare any data from last buffer */
        compare_size = dbtriripple->tri_data_length - dbtriripple->tri_data_counter;

        retval = verify_tri_data(data+data_counter, dbtriripple->tri_data + 
                                 dbtriripple->tri_data_counter, compare_size);
        
        if (retval == -1) {
          multilog (log, LOG_ERR, "Error in the triripple data (3)\n");
        } 

        data_counter += compare_size;
        dbtriripple->tri_data_counter = 0;
        
      }
    
      /* if we have some left over data in that hasn't been compared due to
       * array size differences */
      if (data_counter != data_size) {

        assert(data_counter < data_size);
        compare_size = data_size - data_counter;

        retval = verify_tri_data(data+data_counter, dbtriripple->tri_data + 
                                 dbtriripple->tri_data_counter, compare_size);

        if (retval == -1) {
          multilog (log, LOG_ERR, "Error in the triripple data (4)\n");
        }

        /* since there is not enough space in data to compare all of tri_data,
         * we need to record the tri_data_counter info for the next call to 
         * this function */

        dbtriripple->tri_data_counter = compare_size;
        data_counter += compare_size;
      }
      
    }
  }    

  assert(data_counter == data_size);
  return data_counter;
}

/* Checks that the data in the data array matches the data in the tri_data
 * array up to the compare size index */
int verify_tri_data(char * data, char * tri_data, uint64_t compare_size) {
       
  int i = 0;
  int retval = 1;
  char zerodchar;
  memset(&zerodchar,0,sizeof(char));

  for (i=0; ((retval > 0) && (i < compare_size)); i++) {
    if ((data[i] != tri_data[i]) && (retval == 1)) {
      retval = -1;
    }
    if ((data[i] == zerodchar) && (retval == -1)){
      retval = 0;
    }
  }
  return retval;
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

  /* Quit flag */
  char quit = 0;

  int arg = 0;

  /* Number of bits in triangular wave form data */
  int tri_nbits = 8;

  /* Period of triangular wave form data */
  int tri_period = 512;

  /* actual struct with info */
  dbtriripple_t dbtriripple;

  while ((arg=getopt(argc,argv,"dl:b:v")) != -1)
    switch (arg) {
      
    case 'd':
      daemon=1;
      break;

    case 'l':
      tri_period = atoi(optarg);
      break;
      
    case 'b':
      tri_nbits = atoi(optarg);
      break;

    case 'v':
      verbose=1;
      break;
      
    default:
      usage ();
      return -1;
      
    }

  log = multilog_open ("dada_dbtriripple", daemon);

  if (daemon) {
    be_a_daemon ();
  }
  else
    multilog_add (log, stderr);

  multilog_serve (log, APSR_DEFAULT_DBTRIWAVE_LOG);

  hdu = dada_hdu_create (log);

  if (dada_hdu_connect (hdu) < 0)
    return EXIT_FAILURE;

  if (dada_hdu_lock_read (hdu) < 0)
    return EXIT_FAILURE;

  client = dada_client_create ();

  client->log = log;

  client->data_block = hdu->data_block;
  client->header_block = hdu->header_block;

  client->context        = &dbtriripple;
  client->open_function  = dbtriripple_open_function;
  client->io_function    = dbtriripple_io_function;
  client->close_function = dbtriripple_close_function;
  client->direction      = dada_client_reader;
  client->fd             = 1;                 // not used

  /* Transfer 1 second of data at a time */
  client->transfer_bytes = 64*1024*1024;
  client->optimal_bytes = dbtriripple.tri_data_length*1024;
  
  dbtriripple.tri_data_length = tri_period;
  dbtriripple.tri_data_nbits = tri_nbits;
  dbtriripple.tri_data = malloc(sizeof(char) * dbtriripple.tri_data_length);
  dbtriripple.tri_data_counter = 0;
  dbtriripple.expect_header = 1;
  dbtriripple.verbose = verbose;

  generate_triripple(dbtriripple.tri_data, dbtriripple.tri_data_length,
                                       dbtriripple.tri_data_nbits);

  while (!quit) {
  
    dbtriripple.tri_data_counter = 0;

    if (dada_client_read (client) < 0)
      multilog (log, LOG_ERR, "Error during transfer\n");

  }

  if (dada_hdu_unlock_read (hdu) < 0)
    return EXIT_FAILURE;

  if (dada_hdu_disconnect (hdu) < 0)
    return EXIT_FAILURE;

  return EXIT_SUCCESS;
}

void print_triripple(char *array, int array_size, int nbits) {
                                                                                
  int i = 0;
  unsigned int val = 0;
  printf("[");
  for (i=0; i < array_size; i++) {
    val = (unsigned int) array[i];
    val &= 0x000000ff;
    printf("%u", val);
    if (i != (array_size - 1)) printf(",");
  }
  printf("]\n");
                                                                                
}

/* Generates a triangular wave form in the array of length
 * array size with the mid value being 2*nbits */
void generate_triripple(char *array, int array_size, int nbits) {

  int i = 0;

  float max_value = powf(2.0,(float) nbits) - 1.0;
  float mid_point = ((float) array_size) / 2.0;

  unsigned int val;

  for (i=0; i <= mid_point; i++) {

    val = (unsigned int) (max_value * (((float) i) / mid_point));
    array[i] = val;
    array[array_size-i] = array[i];

  }
}


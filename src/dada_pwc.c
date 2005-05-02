#include "dada_pwc.h"
#include "utc.h"

#include <stdlib.h>
#include <string.h>

/*! Set the command state */
int dada_pwc_command_set (dada_pwc_t* primary, FILE* output,
			      int command, time_t utc, char* header)
{
  int ret = 0;

  if (!primary)
    return -1;

  pthread_mutex_lock(&(primary->mutex));

  while (primary->command != dada_pwc_no_command)
    pthread_cond_wait(&(primary->cond), &(primary->mutex));

  switch (command) {

  case dada_pwc_header:
    if (primary->state != dada_pwc_idle) {
      fprintf (output, "Cannot set header when state is not IDLE\n");
      ret = -1;
    }
    break;

  case dada_pwc_clock:
    if (primary->state != dada_pwc_prepared) {
      fprintf (output, "Cannot start clocking when state is not PREPARED\n");
      ret = -1;
    }
    break;

  case dada_pwc_record_start:
    if (primary->state != dada_pwc_clocking) {
      fprintf (output, "Cannot record start when state is not CLOCKING\n");
      ret = -1;
    }
    break;

  case dada_pwc_record_stop:
    if (primary->state != dada_pwc_recording) {
      fprintf (output, "Cannot record stop when state is not RECORDING\n");
      ret = -1;
    }
    break;

  case dada_pwc_start:
    if (primary->state != dada_pwc_prepared) {
      fprintf (output, "Cannot start when state is not PREPARED\n");
      ret = -1;
    }
    break;

  case dada_pwc_stop:
    if (primary->state != dada_pwc_clocking &&
	primary->state != dada_pwc_recording) {
      fprintf (output, "Cannot stop when state is not CLOCKING or RECORDING\n");
      ret = -1;
    }
    break;

  }

  if (ret == 0) {

    primary->command = command;
    primary->utc = utc;
    
    if (header)
      strcpy (primary->header, header);
    
    pthread_cond_signal (&(primary->cond));

  }

  pthread_mutex_unlock(&(primary->mutex));

  return ret;
}

int dada_pwc_cmd_header (void* context, FILE* fptr, char* args)
{
  dada_pwc_t* primary = (dada_pwc_t*) context;
  char* hdr = args;

  if (strlen (args) > primary->header_size) {
    fprintf (fptr, "header too large (max %d bytes)\n", primary->header_size);
    return -1;
  }

  /* replace \ with new line */
  while ( (hdr = strchr(hdr, '\\')) != 0 )
    *hdr = '\n';

  if (dada_pwc_command_set (primary, fptr, 
				dada_pwc_header, 0, args) < 0)
    return -1;

  return 0;
}

time_t dada_pwc_parse_time (FILE* fptr, char* args)  
{
  time_t utc = 0;

  if (!args)
    return utc;

  utc = str2time (args);
  if (utc == (time_t)-1) {
    fprintf (fptr, "Could not parse start time from '%s'\n", args);
    return -1;
  }

  return utc;
}

int dada_pwc_cmd_clock (void* context, FILE* fptr, char* args)
{
  dada_pwc_t* primary = (dada_pwc_t*) context;
  time_t utc = dada_pwc_parse_time (fptr, args);

  return dada_pwc_command_set (primary, fptr, dada_pwc_clock, utc, 0);
}

int dada_pwc_cmd_record_start (void* context, FILE* fptr, char* args)
{
  dada_pwc_t* primary = (dada_pwc_t*) context;
  time_t utc = dada_pwc_parse_time (fptr, args);

  return dada_pwc_command_set (primary, fptr, dada_pwc_record_start,
				   utc, 0);
}

int dada_pwc_cmd_record_stop (void* context, FILE* fptr, char* args)
{
  dada_pwc_t* primary = (dada_pwc_t*) context;
  time_t utc = dada_pwc_parse_time (fptr, args);

  return dada_pwc_command_set (primary, fptr, dada_pwc_record_stop,
				   utc, 0);
}

int dada_pwc_cmd_start (void* context, FILE* fptr, char* args)
{
  dada_pwc_t* primary = (dada_pwc_t*) context;
  time_t utc = dada_pwc_parse_time (fptr, args);

  return dada_pwc_command_set (primary, fptr, dada_pwc_start, utc, 0);
}

int dada_pwc_cmd_stop (void* context, FILE* fptr, char* args)
{
  dada_pwc_t* primary = (dada_pwc_t*) context;
  time_t utc = dada_pwc_parse_time (fptr, args);

  return dada_pwc_command_set (primary, fptr, dada_pwc_stop, utc, 0);
}

/*! Create a new DADA primary write client connection */
dada_pwc_t* dada_pwc_create ()
{
  dada_pwc_t* primary = (dada_pwc_t*) malloc (sizeof(dada_pwc_t));

  /* default header size */
  primary -> header_size = 4096;
  primary -> header = (char *) malloc (primary->header_size);

  /* default command port */
  primary -> port = 0xdada;

  fprintf (stderr, "dada_pwc on port %d\n", primary->port);

  primary -> state = dada_pwc_idle;
  primary -> command = dada_pwc_no_command;

  /* for multi-threaded use of primary */
  pthread_mutex_init(&(primary->mutex), NULL);
  pthread_cond_init (&(primary->cond), NULL);

  /* command parser */
  primary -> parser = command_parse_create ();

  command_parse_add (primary->parser, dada_pwc_cmd_header, primary,
		     "header", "set the primary header", NULL);

  command_parse_add (primary->parser, dada_pwc_cmd_start, primary,
		     "start", "enter the recording state", NULL);

  command_parse_add (primary->parser, dada_pwc_cmd_stop, primary,
		     "stop", "enter the idle state", NULL);

  command_parse_add (primary->parser, dada_pwc_cmd_clock, primary,
		     "clock", "enter the clocking state", NULL);

  command_parse_add (primary->parser, dada_pwc_cmd_record_start, primary,
		     "rec_start", "enter the recording state", NULL);

  command_parse_add (primary->parser, dada_pwc_cmd_record_stop, primary,
		     "rec_stop", "enter the clocking state", NULL);

  primary -> server = 0;

  return primary;
}

/*! Destroy a DADA primary write client connection */
int dada_pwc_serve (dada_pwc_t* primary)
{
  if (!primary)
    return -1;

  if (primary->server) {
    fprintf (stderr, "dada_pwc_serve: server already launched");
    return -1;
  }

  primary -> server = command_parse_server_create (primary -> parser);

  command_parse_server_set_welcome (primary -> server,
				    "DADA primary write client command");

  /* open the command/control port */
  return command_parse_serve (primary->server, primary->port);
}

/*! Destroy a DADA primary write client connection */
int dada_pwc_destroy (dada_pwc_t* primary)
{
  return 0;
}

/*! Primary write client should exit when this is true */
int dada_pwc_quit (dada_pwc_t* primary)
{
  return 0;
}

/*! Check to see if a command has arrived */
int dada_pwc_command_check (dada_pwc_t* primary)
{
  if (!primary)
    return -1;

  if (primary->command != dada_pwc_no_command)
    return 1;

  return 0;
}

/*! Get the next command from the connection; wait until command received */
int dada_pwc_command_get (dada_pwc_t* primary)
{
  int command = dada_pwc_no_command;
  
  if (!primary)
    return -1;

  pthread_mutex_lock(&(primary->mutex));

  while (primary->command == dada_pwc_no_command)
    pthread_cond_wait(&(primary->cond), &(primary->mutex));

  command = primary->command;

  pthread_mutex_unlock(&(primary->mutex));

  return command;
}

/*! Reply to the last command received */
int dada_pwc_command_ack (dada_pwc_t* primary, int new_state)
{
  if (!primary)
    return -1;

  switch (primary->command) {

  case dada_pwc_no_command:
    fprintf (stderr, "Cannot acknowledge no command\n");
    return -1;

  case dada_pwc_header:
    if (new_state != dada_pwc_prepared) {
      fprintf (stderr, "HEADER acknowledgement state must be PREPARED\n");
      return -1;
    }
    break;

  case dada_pwc_clock:
    if (new_state != dada_pwc_clocking) {
      fprintf (stderr, "CLOCK acknowledgement state must be CLOCKING\n");
      return -1;
    }
    break;

  case dada_pwc_record_start:
    if (new_state != dada_pwc_recording) {
      fprintf (stderr, "REC_START acknowledgement state must be RECORDING\n");
      return -1;
    }
    break;

  case dada_pwc_record_stop:
    if (new_state != dada_pwc_clocking) {
      fprintf (stderr, "REC_STOP acknowledgement state must be CLOCKING\n");
      return -1;
    }
    break;

  case dada_pwc_start:
    if (new_state != dada_pwc_recording) {
      fprintf (stderr, "START acknowledgement state must be RECORDING\n");
      return -1;
    }
    break;

  case dada_pwc_stop:
    if (new_state != dada_pwc_idle) {
      fprintf (stderr, "STOP acknowledgement state must be IDLE\n");
      return -1;
    }
    break;

  }

  pthread_mutex_lock(&(primary->mutex));

  primary->command = dada_pwc_no_command;
  primary->state = new_state;

  pthread_cond_signal (&(primary->cond));
  pthread_mutex_unlock(&(primary->mutex));

  return 0;
}




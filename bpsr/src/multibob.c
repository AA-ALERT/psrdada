/***************************************************************************
 *
 *   Copyright (C) 2008 by Willem van Straten
 *   Licensed under the Academic Free License version 2.1
 *
 ***************************************************************************/

#include "multibob.h"

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

void ibob_thread_init (ibob_thread_t* ibob, int number)
{
  ibob->ibob = ibob_construct ();
  ibob_set_number (ibob->ibob, number);

  pthread_mutex_init(&(ibob->mutex), NULL);
  pthread_cond_init (&(ibob->cond), NULL);
  ibob->id = 0;
  ibob->bramdump = 0;
  ibob->quit = 0;
}

/*! allocate and initialize a new ibob_t struct */
multibob_t* multibob_construct (unsigned nibob)
{
  multibob_t* multibob = malloc (sizeof(multibob_t));

  multibob->threads = malloc (sizeof(ibob_thread_t) * nibob);
  multibob->nthread = nibob;

  unsigned ibob = 0;

  for (ibob = 0; ibob < nibob; ibob++)
    ibob_thread_init (multibob->threads + ibob, ibob + 1);

  multibob->parser = command_parse_create ();
  multibob->server = 0;
  multibob->port = 0;

  command_parse_add (multibob->parser, multibob_cmd_state, multibob,
                     "state", "get the current state", NULL);

  command_parse_add (multibob->parser, multibob_cmd_hostport, multibob,
                     "hostport", "set the hostname and port number", NULL);

  command_parse_add (multibob->parser, multibob_cmd_mac, multibob,
                     "mac", "set the target MAC address", NULL);

  command_parse_add (multibob->parser, multibob_cmd_open, multibob,
                     "open", "open command interface connections", NULL);

  command_parse_add (multibob->parser, multibob_cmd_close, multibob,
                     "close", "close command interface connections", NULL);

  command_parse_add (multibob->parser, multibob_cmd_arm, multibob,
                     "arm", "reset packet count", NULL);

  command_parse_add (multibob->parser, multibob_cmd_quit, multibob,
                     "quit", "quit", NULL);

  return multibob;
}

/*! free all resources reserved for ibob communications */
int multibob_destroy (multibob_t* bob)
{
}

/*!
  The monitor thread simply sits in a loop, opening the connection
  as necessary, and polling the connection every second.  Polling
  can be either a simple ping or a bramdump.  On failure, the
  connection is closed and re-opened ad infinitum every five seconds.
*/

int bramdump (ibob_t* ibob)
{
  fprintf (stderr, "bramdump not implemented\n");
  return 0;
}

void* multibob_monitor (void* context)
{
  if (!context)
    return 0;

  ibob_thread_t* thread = context;
  ibob_t* ibob = thread->ibob;

  while (!thread->quit)
  {
    pthread_mutex_lock (&(thread->mutex));

    fprintf (stderr, "multibob_monitor: opening %s:%d\n",
             ibob->host, ibob->port);

    if ( ibob_open (ibob) < 0 )
    {
      fprintf (stderr, "multibob_monitor: could not open %s:%d - %s\n",
	       ibob->host, ibob->port, strerror(errno));

      pthread_mutex_unlock (&(thread->mutex));

      if (thread->quit)
        break;

      sleep (5);
      continue;
    }

    pthread_mutex_unlock (&(thread->mutex));

    while (!thread->quit)
    {
      int retval = 0;

      pthread_mutex_lock (&(thread->mutex));

      fprintf (stderr, "multibob_monitor: ping %s:%d\n",
               ibob->host, ibob->port);

      if (thread->bramdump)
	retval = bramdump (ibob);
      else
	retval = ibob_ping (ibob);

      pthread_mutex_unlock (&(thread->mutex));

      if (retval < 0)
      {
        fprintf (stderr, "multibob_monitor: communicaton failure on %s:%d\n",
                 ibob->host, ibob->port);
	break;
      }

      if (thread->quit)
        break;

      sleep (1);
    }

    fprintf (stderr, "multibob_monitor: closing connection with %s:%d\n",
             ibob->host, ibob->port);

    ibob_close (ibob);
  }

  return 0;
}

/*! open the command connections to all of the ibobs */
int multibob_cmd_open (void* context, FILE* fptr, char* args)
{
  if (!context)
    return -1;

  multibob_t* multibob = context;

  unsigned ibob = 0;
  for (ibob = 0; ibob < multibob->nthread; ibob++)
  {
    ibob_thread_t* thread = multibob->threads + ibob;

    if (thread->id == 0)
    {
      thread->quit = 0;

      errno = pthread_create (&(thread->id), 0, multibob_monitor, thread);
      if (errno)
	fprintf (stderr, "multibob_cmd_open: error starting thread %d - %s\n",
		 ibob, strerror (errno));
    }
  }
}

/*! close the command connections to all of the ibobs */
int multibob_cmd_close (void* context, FILE* fptr, char* args)
{
  if (!context)
    return -1;

  multibob_t* multibob = context;

  unsigned ibob = 0;
  for (ibob = 0; ibob < multibob->nthread; ibob++)
  {
    ibob_thread_t* thread = multibob->threads + ibob;
    thread -> quit = 1;
  }

  for (ibob = 0; ibob < multibob->nthread; ibob++)
  {
    ibob_thread_t* thread = multibob->threads + ibob;
    if (thread->id)
    {
      void* result = 0;
      pthread_join (thread->id, &result);
      thread->id = 0;
    }
  }
}

/*! reset packet counter on next UTC second, returned */
int multibob_cmd_state (void* context, FILE* fptr, char* args)
{
}

/*! set the host and port number of the specified ibob */
int multibob_cmd_hostport (void* context, FILE* fptr, char* args)
{
}

/*! set the target MAC address of the specified ibob */
int multibob_cmd_mac (void* context, FILE* fptr, char* args)
{
}

/*! reset packet counter on next UTC second, returned */
int multibob_cmd_arm (void* context, FILE* fptr, char* args)
{
}

/*! reset packet counter on next UTC second, returned */
int multibob_cmd_quit (void* context, FILE* fptr, char* args)
{
}

/*! mutex lock all of the ibob interfaces */
void multibob_lock (multibob_t* bob)
{
}

/*! mutex unlock all of the ibob interfaces */
void multibob_unlock (multibob_t* bob)
{
}

/*! */
int multibob_serve (multibob_t* bob)
{
  if (!bob)
    return -1;

  if (bob->port)
  {
    if (bob->server)
    {
      fprintf (stderr, "multibob_serve: server already launched \n");
      return -1;
    }

    bob -> server = command_parse_server_create (bob -> parser);

    command_parse_server_set_welcome (bob -> server,
				      "multibob command");

    /* open the command/control port */
    command_parse_serve (bob->server, bob->port);

    void* result = 0;
    pthread_join (bob->server->thread, &result);
  }
  else
  {
    fprintf (stderr, "multibob_serve: stdin/out interface not implemented \n");
    return -1;
  }
}

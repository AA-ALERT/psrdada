#ifndef __DADA_MULTILOG_H
#define __DADA_MULTILOG_H

/* ************************************************************************

   ************************************************************************ */

#include <syslog.h>
#include <stdio.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

  typedef struct {

    char     syslog;  /* set to true to use syslog */
    FILE**   logs;    /* streams to which messages will be written */
    unsigned nlog;    /* number of streams */

    /* for multi-threaded use of the multilog */
    pthread_mutex_t mutex;
    pthread_t thread;

    int port;         /* the port on which multilog_server is listening */

  } multilog_t;

  /*! Open a multilogger */
  multilog_t* multilog_open (char syslog);

  /*! Close a multilogger */
  int multilog_close (multilog_t* m);

  /*! Add a listener to the multilog */
  int multilog_add (multilog_t* m, FILE* fptr);

  /*! Write a message to all listening streams */
  int multilog (multilog_t* m, int priority, const char* format, ...);

  /*! Start another thread to receive log socket connections */
  int multilog_serve (multilog_t* m, int port);

#ifdef __cplusplus
	   }
#endif

#endif

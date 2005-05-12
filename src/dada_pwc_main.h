#ifndef __DADA_PWC_MAIN_H
#define __DADA_PWC_MAIN_H

/* ************************************************************************

   dada_pwc_main_t - a struct and associated routines for creation and
   execution of a dada primary write client main loop

   ************************************************************************ */

#include "dada_pwc.h"
#include "multilog.h"

#ifdef __cplusplus
extern "C" {
#endif

  typedef struct dpwcm {

    /*! The primary write client control connection */
    dada_pwc_t* pwc;

    /*! The current command from the PWC control connection */
    dada_pwc_command_t command;

    /*! The status and error logging interface */
    multilog_t* log;

    /*! Pointer to the function that starts data transfer */
    time_t (*start_function) (struct dpwcm*, time_t);

    /*! Pointer to the function that returns a data buffer */
    void* (*buffer_function) (struct dpwcm*, uint64_t* size);

    /*! Pointer to the function that stops data transfer */
    int (*stop_function) (struct dpwcm*);

    /*! Additional context information */
    void* context;

  } dada_pwc_main_t;

  /*! Create a new DADA primary write client main loop */
  dada_pwc_main_t* dada_pwc_main_create ();

  /*! Destroy a DADA primary write client main loop */
  void dada_pwc_main_destroy (dada_pwc_main_t* primary);

  /*! Run the DADA primary write client main loop */
  int dada_pwc_main (dada_pwc_main_t* main);

#ifdef __cplusplus
	   }
#endif

#endif

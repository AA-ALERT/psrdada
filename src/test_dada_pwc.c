#include "dada_pwc_main.h"

#include <unistd.h> /* sleep */

time_t fake_start_function (dada_pwc_main_t* pwcm, time_t utc)
{
  time_t now = time(0);
  unsigned sleep_time = 0;

  if (utc > now) {
    sleep_time = utc - now;
    multilog (pwcm->log, LOG_INFO, "sleeping for %u sec\n", sleep_time);
    sleep (sleep_time);
    return utc;
  }

  return now;
}

void* fake_buffer_function (dada_pwc_main_t* pwcm, uint64_t* size)
{
  *size = pwcm->pwc->bytes_per_second;
  return (void*) 1;
}

int fake_stop_function (dada_pwc_main_t* pwcm)
{
  return 0;
}

int main ()
{
  dada_pwc_main_t* pwcm = 0;

  fprintf (stderr, "Creating dada pwc main\n");
  pwcm = dada_pwc_main_create ();

  pwcm->start_function  = fake_start_function;
  pwcm->buffer_function = fake_buffer_function;
  pwcm->stop_function   = fake_stop_function;

  fprintf (stderr, "Creating dada pwc control interface\n");
  pwcm->pwc = dada_pwc_create ();

  if (dada_pwc_serve (pwcm->pwc) < 0) {
    fprintf (stderr, "test_dada_pwc: could not start server\n");
    return -1;
  }

  pwcm->log = multilog_open ("test_dada_pwc", 0);
  multilog_add (pwcm->log, stderr);

  if (dada_pwc_main (pwcm) < 0) {
    fprintf (stderr, "test_dada_pwc: error in main loop\n");
    return -1;
  }

  fprintf (stderr, "Destroying pwc\n");
  dada_pwc_destroy (pwcm->pwc);

  fprintf (stderr, "Destroying pwc main\n");
  dada_pwc_main_destroy (pwcm);

  return 0;
}


#include <stdio.h>
#include <inttypes.h>

int main ()
{
  uint64_t value = UINT64_MAX;
  char text[64];

  sprintf (text, "%llu\n", value);

  value = 0;

  if (sscanf (text, "%llu\n", &value) != 1) {
    fprintf (stderr, "test_uint64_t: error parsing %%llu\n");
    return -1;
  }

  if (value != UINT64_MAX) {
    fprintf (stderr, "test_uint64_t: error parsing UINT64_MAX\n");
    return -1;
  }

  return 0;
}

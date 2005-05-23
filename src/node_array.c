#include "node_array.h"
#include "sock.h"

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

// #define _DEBUG 1

/*! Create a new node array */
node_array_t* node_array_create ()
{
  node_array_t* array = (node_array_t*) malloc (sizeof(node_array_t));
  assert (array != 0);

  array -> nodes = 0;
  array -> nnode = 0;
  array -> cnode = 0;

  pthread_mutex_init(&(array->mutex), NULL);

  return array;
}

/*! Destroy a node array */
int node_array_destroy (node_array_t* array)
{
  if (!array)
    return -1;

  if (array->nodes)
    free (array->nodes);

  pthread_mutex_destroy (&(array->mutex));

  return 0;
}

/*! Add a node to the array */
int node_array_add (node_array_t* array, const char* name, int port)
{
  /* pointer to new node_t structure in array */
  node_t* new_node = 0;

  /* array counter */
  unsigned inode;

  /* the new file descriptor for the open socket */
  int fd = sock_open (name, port);
  if (fd < 0) {
    fprintf (stderr, "node_array_add: error sock_open(%s,%d) %s \n",
	     name, port, strerror(errno));
    return -1;
  }

  pthread_mutex_lock (&(array->mutex));

  /* ensure that each node in array is a unique device */
  for (inode = 0; inode < array->nnode; inode++) {
    if (!strcmp (name, array->nodes[inode].name)) {
      fprintf (stderr, "node_array_add: %s is already in array\n", name);
      close (fd);
      pthread_mutex_unlock (&(array->mutex));
      return -1;
    }
  }

  array->nodes = realloc (array->nodes, (array->nnode+1)*sizeof(node_t));
  assert (array->nodes != 0);

  new_node = array->nodes + array->nnode;
  array->nnode ++;

  new_node->fd = fd;
  new_node->space = 0;
  new_node->port = port;
  new_node->name = strdup (name);

  assert (new_node->name != 0);

  pthread_mutex_unlock (&(array->mutex));

  return 0;
}

/*! Get the available amount of node space */
uint64_t node_array_get_available (node_array_t* array)
{
  uint64_t available_space = 0;
  unsigned inode;

  pthread_mutex_lock (&(array->mutex));

  for (inode = 0; inode < array->nnode; inode++)
    available_space += array->nodes[inode].space;

  pthread_mutex_unlock (&(array->mutex));

  return available_space;
}

/*! Open a file on the node array, return the open file descriptor */
int node_array_open (node_array_t* array, uint64_t filesize,
		     uint64_t* optimal_buffer_size)
{
  int fd;

  pthread_mutex_lock (&(array->mutex));
  array->cnode %= array->nnode;
  fd = array->nodes[array->cnode].fd;
  array->cnode ++;
  pthread_mutex_unlock (&(array->mutex));

  return fd;

}

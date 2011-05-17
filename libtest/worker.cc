/* Gearman server and library
 * Copyright (C) 2008 Brian Aker, Eric Day
 * All rights reserved.
 *
 * Use and distribution licensed under the BSD license.  See
 * the COPYING file in the parent directory for full text.
 */

#include "config.h"

#include <cassert>
#include <cstring>
#include <signal.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
#include <pthread.h>

#include <stdio.h>

#include "libtest/worker.h"

#ifndef __INTEL_COMPILER
#pragma GCC diagnostic ignored "-Wold-style-cast"
#endif

struct context_st {
  in_port_t port;
  const char *function_name;
  gearman_worker_fn *function;
  void *function_arg;
  struct worker_handle_st *handle;
  gearman_worker_options_t options;
  struct gearman_reducer_t reducer;
  gearman_aggregator_fn *aggregator_fn;
};

static void *thread_runner(void *con)
{
  struct context_st *context= (struct context_st *)con;
  gearman_worker_st worker, *worker_ptr;

  worker_ptr= gearman_worker_create(&worker);
  assert(worker_ptr);

  gearman_return_t rc;
  rc= gearman_worker_add_server(&worker, NULL, context->port);
  assert(rc == GEARMAN_SUCCESS);

  bool success= gearman_worker_set_server_option(&worker, gearman_literal_param("exceptions"));
  assert(success);

  if (context->aggregator_fn)
  {
    rc= gearman_worker_add_map_function(&worker,
                                        context->function_name, strlen(context->function_name), 
                                        0, 
                                        context->function,
                                        context->aggregator_fn,
                                        context->function_arg);
  }
  else
  {
    rc= gearman_worker_add_function(&worker, context->function_name, 0, context->function,
                                    context->function_arg);
  }
  assert(rc == GEARMAN_SUCCESS);

  if (context->options != gearman_worker_options_t())
  {
    gearman_worker_add_options(&worker, context->options);
  }

  gearman_worker_set_timeout(&worker, 100);

  assert(context->handle);

  if (context->reducer.each_fn)
  {
    gearman_worker_set_reducer(worker_ptr,  context->reducer);
  }

  while (context->handle->shutdown == false)
  {
    gearman_return_t ret= gearman_worker_work(&worker);
    (void)ret;
  }

  gearman_worker_free(&worker);

  free(context);

  pthread_exit(0);
}

struct worker_handle_st *test_worker_start(in_port_t port, const char *function_name,
                                           gearman_worker_fn *function, void *function_arg,
                                           gearman_worker_options_t options)
{
  gearman_reducer_t reducer= gearman_reducer_make(0);
  return test_worker_start_with_reducer(port, function_name, function, function_arg, options, reducer, 0);
}

struct worker_handle_st *test_worker_start_with_reducer(in_port_t port, const char *function_name,
							gearman_worker_fn *function, void *function_arg,
							gearman_worker_options_t options,
							struct gearman_reducer_t reducer,
							gearman_aggregator_fn *aggregator_fn)
{
  pthread_attr_t attr;

  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

  struct worker_handle_st *handle= (struct worker_handle_st *)calloc(1, sizeof(struct worker_handle_st));
  assert(handle);

  struct context_st *foo= (struct context_st *)calloc(1, sizeof(struct context_st));

  foo->port= port;
  foo->function_name= function_name;
  foo->function= function;
  foo->function_arg= function_arg;
  foo->handle= handle;
  foo->options= options;
  foo->reducer= reducer;
  foo->aggregator_fn= aggregator_fn;

  int rc= pthread_create(&handle->thread, &attr, thread_runner, foo);
  assert(rc == 0);

  pthread_attr_destroy(&attr);

  /* Wait for the server to start and bind the port. */
  struct timespec dream, rem;

  dream.tv_nsec= 0;
  dream.tv_sec= 1;

  nanosleep(&dream, &rem);

  return handle;
}

void test_worker_stop(struct worker_handle_st *handle)
{
  void *unused;
  fflush(stderr);
  handle->shutdown= true;
  pthread_join(handle->thread, &unused);
  free(handle);
}

/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef OAV_JOBS_H
#define OAV_JOBS_H
#include "oav_core.h"
#include "oap.h"
typedef struct OAVRequest {
 char source[OAV_PATH_MAX],output[OAV_PATH_MAX],uri[OAV_PATH_MAX];
 char action[16];OAVLayout layout;
 int has_print_options;OAPJobOptions print;
} OAVRequest;
int oav_submit(const OAVRequest *r,char *request,size_t cap,char *err,size_t errcap);
int oav_read_request(const char *path,OAVRequest *r);
int oav_result(const char *request,char *state,size_t statecap,char *message,size_t msgcap);
int oav_cancel(const char *request);
int oav_result_terminal(const char *state);
int oav_update_queue_state(const char *,const char *,const char *,const char *);
#endif

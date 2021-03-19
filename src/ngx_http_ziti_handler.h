/*
Copyright Netfoundry, Inc.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

https://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#ifndef NGX_HTTP_ZITI_HANDLER_H
#define NGX_HTTP_ZITI_HANDLER_H

#include <ngx_core.h>
#include <ngx_http.h>


typedef enum ZITI_REQ_STATE_tag
{
    ZS_REQ_INIT = 0,
    ZS_REQ_PROCESSING,
    ZS_RESP_HEADER_TRANSMIT_REQUIRED,
    ZS_RESP_BODY_CHUNK_TRANSMIT_REQUIRED,
    ZS_RESP_BODY_DONE
} ZITI_REQ_STATE;




typedef void(*ngx_http_ziti_request_callback_t)(void* context, ngx_int_t rc);

typedef struct ngx_http_ziti_request_ctx_s ngx_http_ziti_request_ctx_t;


typedef struct HttpsRespItem {
  um_http_req_t *req;
  int code;
  char* status;
  um_http_hdr *headers;
} HttpsRespItem;


typedef struct HttpsRespBodyItem {
  um_http_req_t *req;
  const void *body;
  ssize_t len;
} HttpsRespBodyItem;


typedef struct HttpsReq {
    um_http_req_t *req;
    bool on_resp_has_fired;
    int respCode;
    struct ngx_http_ziti_request_ctx_s   *request_ctx;
} HttpsReq;


typedef struct {
    char* scheme_host_port;
    um_http_t client;
    um_src_t ziti_src;
    bool active;
    bool purge;
} HttpsClient;


typedef struct ngx_http_ziti_request_ctx_s {
    ZITI_REQ_STATE                      state;    
    ngx_http_request_t                 *r;
    ngx_pool_t                          *pool;
    ngx_uint_t                          status;
    um_src_t                            zs;
    um_http_t                           clt;
    size_t                              buf_size;

    ngx_chain_t                        *out_bufs;
    uv_sem_t                            out_bufs_sem;

    ngx_chain_t                       **last_out;
    
    ngx_buf_t                          *out_buf;

    ngx_buf_t                           cached;
    ngx_buf_t                           postponed;
    size_t                              avail_out;
    ngx_chain_t                         out_chain;
    ngx_http_ziti_request_callback_t    callback;
    ngx_int_t                           err;
    uv_work_t                           uv_req;
    HttpsClient                        *httpsClient;
    HttpsReq                           *httpsReq;
    char                               *scheme_host_port;

} ngx_http_ziti_request_ctx_t;


typedef struct {
    ngx_http_request_t *r;
    ngx_http_ziti_request_ctx_t *request_ctx;
    ngx_http_ziti_loc_conf_t *zlcf;
} ngx_http_ziti_um_http_req_thread_ctx_t;

typedef struct {
    ngx_http_request_t *r;
    ngx_http_ziti_request_ctx_t *request_ctx;
    ngx_http_ziti_loc_conf_t *zlcf;
} ngx_http_ziti_req_complete_thread_ctx_t;

typedef struct {
    ngx_http_request_t *r;
    ngx_http_ziti_request_ctx_t *request_ctx;
    ngx_http_ziti_loc_conf_t *zlcf;
} ngx_http_ziti_resp_chunk_thread_ctx_t;

typedef struct {
    ngx_http_request_t *r;
    ngx_http_ziti_request_ctx_t *request_ctx;
    ngx_http_ziti_loc_conf_t *zlcf;
} ngx_http_ziti_resp_header_transmit_thread_ctx_t;


ngx_int_t ngx_http_ziti_handler(ngx_http_request_t *r);


#endif /* NGX_HTTP_ZITI_HANDLER_H */

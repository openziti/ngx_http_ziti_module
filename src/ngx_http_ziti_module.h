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

#ifndef NGX_HTTP_ZITI_MODULE_H
#define NGX_HTTP_ZITI_MODULE_H


#include <nginx.h>
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


#include <ziti/ziti.h>
#include <uv.h>
#include <ziti/ziti_src.h>
#include <ziti/ziti_log.h>


#ifndef NGX_HTTP_GONE
#define NGX_HTTP_GONE  410
#endif

#define ngx_http_ziti_module_version  101
#define ngx_http_ziti_module_version_string  "0.1.1"


#define ngx_str_last(str)            (u_char *) ((str)->data + (str)->len)
#define ngx_conf_str_empty(str)      ((str)->sv.len == 0 && (str)->cv == NULL)
#define ngx_http_ziti_strcmp_const(a, b) ngx_strncmp(a, b, sizeof(b) - 1)

extern uv_loop_t *uv_thread_loop;

static ngx_str_t ngx_http_ziti_thread_pool_name = ngx_string("ziti");

extern ngx_module_t ngx_http_ziti_module;

typedef struct {
    ngx_str_t                   name;
    ngx_str_t                   sv;
    ngx_http_complex_value_t   *cv;
    ngx_str_t                  *cmd;
} ngx_conf_str_t;


typedef struct {
    ngx_uint_t                          key;
    ngx_str_t                           sv;
    ngx_http_complex_value_t           *cv;
} ngx_ziti_mixed_t;


typedef struct {
    u_char                             *name;
    uint32_t                            key;
} ngx_ziti_http_method_t;



typedef enum ZITI_LOC_STATE_tag
{
    ZS_LOC_INIT = 0,
    ZS_LOC_UV_LOOP_STARTED,
    ZS_LOC_ZITI_INIT_STARTED,
    ZS_LOC_ZITI_INIT_COMPLETED,
    ZS_LOC_ZITI_LAST
} ZITI_LOC_STATE;


typedef struct {
    uv_loop_t                          *uv_thread_loop;
    ZITI_LOC_STATE                      state;    
    ngx_pool_t                          *pool;
    /* abs path to ziti identity */
    char                               *identity_path;
    /* ziti service name */
    char                               *servicename;
    size_t                               buf_size;
    uv_thread_t                          thread;
    uv_async_t                           async;
    ziti_context                         ztx;
	ngx_thread_pool_t                   *thread_pool;
    size_t                               client_pool_size;
} ngx_http_ziti_loc_conf_t;


typedef struct {
    ngx_int_t                           status;
} ngx_http_ziti_ctx_t;


typedef struct {
    ngx_http_request_t *r;
    ngx_http_ziti_loc_conf_t *zlcf;
} ngx_http_ziti_uv_run_thread_ctx_t;

typedef struct {
    ngx_http_request_t *r;
    ngx_http_ziti_loc_conf_t *zlcf;
} ngx_http_ziti_await_init_thread_ctx_t;


#endif /* NGX_HTTP_ZITI_MODULE_H */

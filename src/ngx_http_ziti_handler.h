#ifndef NGX_HTTP_ZITI_HANDLER_H
#define NGX_HTTP_ZITI_HANDLER_H

#include <ngx_core.h>
#include <ngx_http.h>


typedef enum ZITI_REQ_STATE_tag
{
    ZS_REQ_INIT = 0,
    ZS_REQ_ALLOC_CLIENT,
    ZS_REQ_PROCESSING,
    ZS_REQ_DONE
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
    // ngx_uint_t                          done;
    ngx_uint_t                          status;
    um_src_t                            zs;
    um_http_t                           clt;
    ngx_buf_t                          *b;
    ngx_chain_t                         out;
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



ngx_int_t ngx_http_ziti_handler(ngx_http_request_t *r);


#endif /* NGX_HTTP_ZITI_HANDLER_H */

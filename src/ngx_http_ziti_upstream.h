#ifndef NGX_HTTP_ZITI_UPSTREAM_H
#define NGX_HTTP_ZITI_UPSTREAM_H


#include <ngx_core.h>
#include <ngx_http.h>
#include <nginx.h>
#include "ngx_http_ziti_module.h"


typedef struct {

    ngx_pool_t       *pool;

} ngx_http_upstream_ziti_srv_conf_t;


void *ngx_http_upstream_ziti_create_srv_conf(ngx_conf_t *cf);


#endif /* NGX_HTTP_ZITI_UPSTREAM_H */

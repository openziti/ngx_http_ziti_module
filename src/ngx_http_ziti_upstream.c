
#ifndef DDEBUG
#define DDEBUG 1
#endif
#include "ddebug.h"

#include "ngx_http_ziti_module.h"
#include "ngx_http_ziti_upstream.h"

#define ZITI_MAX_SERVICE_SIZE 128


void *
ngx_http_upstream_ziti_create_srv_conf(ngx_conf_t *cf)
{
    ngx_pool_cleanup_t                   *cln;
    ngx_http_upstream_ziti_srv_conf_t    *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_upstream_ziti_srv_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    conf->pool = cf->pool;

    cln = ngx_pool_cleanup_add(cf->pool, 0);

    return conf;
}

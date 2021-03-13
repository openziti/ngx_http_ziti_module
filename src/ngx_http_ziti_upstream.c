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

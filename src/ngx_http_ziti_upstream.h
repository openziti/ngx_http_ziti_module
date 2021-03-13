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

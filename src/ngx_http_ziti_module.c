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
#include "ngx_http_ziti_handler.h"
#include "ngx_http_ziti_upstream.h"


#ifndef NGX_THREADS
#error ngx_http_ziti_module requires --with-threads
#endif /* NGX_THREADS */


/* Forward declaration */

static ngx_int_t ngx_http_ziti_preconfiguration(ngx_conf_t *cf);
static ngx_int_t ngx_http_ziti_postconfiguration(ngx_conf_t *cf);
static char *ngx_http_ziti_pass(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char *ngx_http_ziti_identity(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char *ngx_http_ziti_client_pool_size(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char *ngx_http_ziti_thread_pool(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static void *ngx_http_ziti_create_loc_conf(ngx_conf_t *cf);
static char *ngx_http_ziti_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child);


/* config directives for ngx_http_ziti module */
static ngx_command_t ngx_http_ziti_cmds[] = {

    { ngx_string("ziti_pass"),
      NGX_HTTP_LOC_CONF|NGX_CONF_1MORE,
      ngx_http_ziti_pass,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("ziti_client_pool_size"),
      NGX_HTTP_LOC_CONF|NGX_CONF_1MORE,
      ngx_http_ziti_client_pool_size,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("ziti_identity"),
      NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF|NGX_CONF_TAKE1,
      ngx_http_ziti_identity,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    ngx_null_command
};


/* Nginx HTTP subsystem module hooks */
static ngx_http_module_t ngx_http_ziti_module_ctx = {
    ngx_http_ziti_preconfiguration,
            /* preconfiguration */
    ngx_http_ziti_postconfiguration,
            /* postconfiguration */

    NULL,    /* create_main_conf */
    NULL,    /* merge_main_conf */

    ngx_http_upstream_ziti_create_srv_conf,
             /* create_srv_conf */
    NULL,    /* merge_srv_conf */

    ngx_http_ziti_create_loc_conf,    /* create_loc_conf */
    ngx_http_ziti_merge_loc_conf      /* merge_loc_conf */
};


ngx_module_t ngx_http_ziti_module = {
    NGX_MODULE_V1,
    &ngx_http_ziti_module_ctx,       /* module context */
    ngx_http_ziti_cmds,              /* module directives */
    NGX_HTTP_MODULE,                 /* module type */
    NULL,    /* init master */
    NULL,    /* init module */
    NULL,    /* init process */
    NULL,    /* init thread */
    NULL,    /* exit thread */
    NULL,    /* exit process */
    NULL,    /* exit master */
    NGX_MODULE_V1_PADDING
};


ngx_ziti_http_method_t ngx_ziti_http_methods[] = {
    { (u_char *) "GET",       (uint32_t) NGX_HTTP_GET },
    { (u_char *) "HEAD",      (uint32_t) NGX_HTTP_HEAD },
    { (u_char *) "POST",      (uint32_t) NGX_HTTP_POST },
    { (u_char *) "PUT",       (uint32_t) NGX_HTTP_PUT },
    { (u_char *) "DELETE",    (uint32_t) NGX_HTTP_DELETE },
    { (u_char *) "OPTIONS",   (uint32_t) NGX_HTTP_OPTIONS },
    { (u_char *) "PATCH",     (uint32_t) NGX_HTTP_PATCH },
    { NULL, 0 }
};


uv_loop_t *uv_thread_loop;

static const char *ALL_CONFIG_TYPES[] = {
        "all",
        NULL
};


/**
 * 
 */
static ngx_int_t
ngx_http_ziti_preconfiguration(ngx_conf_t *cf)
{
    ngx_thread_pool_t          *tp;

    tp = ngx_thread_pool_add(cf, &ngx_http_ziti_thread_pool_name);

    if (tp == NULL) {
        return NGX_ERROR;
    }

    uv_thread_loop = uv_default_loop();

    return NGX_OK;
}


/**
 * 
 */
static ngx_int_t
ngx_http_ziti_postconfiguration(ngx_conf_t *cf)
{
    return NGX_OK;
}



static char *
ngx_conf_str_set(ngx_conf_t *cf, ngx_conf_str_t *cfs, ngx_str_t *s,
    const char *name, ngx_str_t *cmd, ngx_int_t not_empty)
{
    ngx_http_compile_complex_value_t  ccv;

    if (s->len == 0 && not_empty) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "config string \"%s\" is empty in \"%V\" directive",
                           name, cmd);
        return NGX_CONF_ERROR;
    }

    if (s->len > 0 && s->data[0] == '=') {
        cfs->sv.data = s->data + 1;
        cfs->sv.len = s->len - 1;
        cfs->cv = NULL;

    } else if (ngx_http_script_variables_count(s)) {

        cfs->cv = ngx_palloc(cf->pool, sizeof(ngx_http_complex_value_t));
        if (cfs->cv == NULL) {
            return NGX_CONF_ERROR;
        }

        ngx_memzero(&ccv, sizeof(ngx_http_compile_complex_value_t));

        ccv.cf = cf;
        ccv.value = s;
        ccv.complex_value = cfs->cv;

        if (ngx_http_compile_complex_value(&ccv) != NGX_OK) {
            return NGX_CONF_ERROR;
        }

    } else {
        cfs->sv = *s;
        cfs->cv = NULL;
    }

    cfs->name.data = (u_char *) name;
    cfs->name.len = ngx_strlen(name);

    cfs->cmd = cmd;

    return NGX_CONF_OK;
}


static void *
ngx_http_ziti_create_loc_conf(ngx_conf_t *cf)
{
    ngx_http_ziti_loc_conf_t             *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_ziti_loc_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    conf->buf_size = NGX_CONF_UNSET_SIZE;

    return conf;
}


static char *
ngx_http_ziti_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_http_ziti_loc_conf_t *prev = parent;
    ngx_http_ziti_loc_conf_t *conf = child;

    ngx_conf_merge_size_value(conf->buf_size, prev->buf_size, (size_t) ngx_pagesize);

    return NGX_CONF_OK;
}


static char *
ngx_http_ziti_pass(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_core_loc_conf_t   *clcf;
    ngx_http_ziti_loc_conf_t   *zlcf = conf;
    ngx_str_t                  *value = cf->args->elts;
    ngx_conf_str_t              servicename;

    if (zlcf->servicename != NULL) {
        return "is duplicate";
    }

    if (ngx_conf_str_set(cf, &servicename, &value[1], "ziti_pass", &cmd->name, 1))
    {
        return NGX_CONF_ERROR;
    }

    ZITI_LOG(INFO, "servicename is: %s", servicename.sv.data);

    zlcf->servicename = strdup((char*)servicename.sv.data);  

    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);

    clcf->handler = ngx_http_ziti_handler;

    return NGX_CONF_OK;
}


static void 
nop(uv_async_t *handle, int status) { }


static void 
uv_thread_loop_func(void *data){
    uv_loop_t *thread_loop = (uv_loop_t *) data;

    //Start the loop
    uv_run(thread_loop, UV_RUN_DEFAULT);
}


/**
 * 
 */
static void on_ziti_event(ziti_context _ztx, const ziti_event_t *event) {

    ngx_http_ziti_loc_conf_t *zlcf;

    switch (event->type) {

    case ZitiContextEvent:

        zlcf = (ngx_http_ziti_loc_conf_t*)ziti_app_ctx(_ztx);

        // Save the global ztx context variable in the zlcf
        zlcf->ztx = _ztx;

        if (event->event.ctx.ctrl_status == ZITI_OK) {

            const ziti_version *ctrl_ver = ziti_get_controller_version(_ztx);
            const ziti_identity *proxy_id = ziti_get_identity(_ztx);

            ZITI_LOG(INFO, "controller version = %s(%s)[%s]", ctrl_ver->version, ctrl_ver->revision, ctrl_ver->build_date);
            ZITI_LOG(INFO, "identity = <%s>[%s]@%s", proxy_id->name, proxy_id->id, ziti_get_controller(zlcf->ztx));

            zlcf->state = ZS_LOC_ZITI_INIT_COMPLETED;

        }
        else {

            ZITI_LOG(ERROR, "Failed to connect to controller: %s", event->event.ctx.err);

            exit(-1);
        }
        break;

    default:
        break;
    }
}


ngx_int_t
ngx_http_ziti_start_uv_loop(ngx_http_ziti_loc_conf_t *zlcf, ngx_log_t *log)
{
    ngx_int_t                      rc;

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, log, 0, "ngx_http_ziti_start_uv_loop: entered");

    zlcf->ztx = NGX_CONF_UNSET_PTR;
    zlcf->uv_thread_loop = uv_loop_new();
    uv_async_init(zlcf->uv_thread_loop, &zlcf->async, (uv_async_cb)nop);

    // Create the libuv thread loop
    zlcf->uv_thread_loop = uv_loop_new();
    uv_async_init(zlcf->uv_thread_loop, &zlcf->async, (uv_async_cb)nop);
    uv_thread_create(&zlcf->thread, (uv_thread_cb)uv_thread_loop_func, zlcf->uv_thread_loop);

    ziti_options *opts = ngx_calloc(sizeof(ziti_options), log);

    opts->config = (char*)zlcf->identity_path;

    opts->events = ZitiContextEvent;
    opts->event_cb = on_ziti_event;
    opts->refresh_interval = 60;
    opts->router_keepalive = 10;
    opts->app_ctx = zlcf;
    opts->config_types = ALL_CONFIG_TYPES;
    opts->metrics_type = INSTANT;

    rc = ziti_init_opts(opts, zlcf->uv_thread_loop);

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, log, 0, "ziti_init_opts returned %d", rc);

    return NGX_OK;
}


static char *
ngx_http_ziti_identity(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_ziti_loc_conf_t   *zlcf = conf;
    ngx_str_t                  *value = cf->args->elts;
    ngx_conf_str_t              identity_path;

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, cf->log, 0, "ngx_http_ziti_identity: entered");

    if (zlcf->identity_path != NULL) {
        return "is duplicate";
    }

    zlcf->pool = ngx_create_pool(NGX_DEFAULT_POOL_SIZE, cf->log);
    if (zlcf->pool == NULL) {
        return NGX_CONF_ERROR;
    }

    if (ngx_conf_str_set(cf, &identity_path, &value[1], "ziti_identity", &cmd->name, 1))
    {
        return NGX_CONF_ERROR;
    }

    zlcf->identity_path = strdup((char*)identity_path.sv.data);  

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, cf->log, 0, "identity_path is: %s", zlcf->identity_path);

    ngx_http_ziti_start_uv_loop(zlcf, cf->log);

    return NGX_CONF_OK;
}


static char *
ngx_http_ziti_client_pool_size(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_ziti_loc_conf_t                    *zlcf = conf;
    ngx_str_t                                   *value;
    ngx_uint_t                                   i;
    ngx_int_t                                    n;
    u_char                                      *data;
    ngx_uint_t                                   len;

    if (zlcf->client_pool_size) {
        return "is duplicate";
    }

    value = cf->args->elts;

    for (i = 1; i < cf->args->nelts; i++) {

        if (ngx_http_ziti_strcmp_const(value[i].data, "max=") == 0)
        {
            len = value[i].len - (sizeof("max=") - 1);
            data = &value[i].data[sizeof("max=") - 1];

            n = ngx_atoi(data, len);

            if (n == NGX_ERROR || n < 5) {  // enforce a minimum of 5 clients

                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                                   "invalid \"max\" value \"%V\" "
                                   "in \"%V\" directive; must be at least 5",
                                   &value[i], &cmd->name);

                return NGX_CONF_ERROR;
            }

            zlcf->client_pool_size = n;

            ZITI_LOG(INFO, "client_pool_size is: %d", zlcf->client_pool_size);

            continue;
        }

        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "ngx_http_ziti_module: invalid parameter \"%V\" in"
                           " \"%V\" directive",
                           &value[i], &cmd->name);

        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}

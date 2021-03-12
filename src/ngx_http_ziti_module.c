
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
static char *ngx_http_ziti_thread_pool(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static void *ngx_http_ziti_create_loc_conf(ngx_conf_t *cf);
static char *ngx_http_ziti_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child);

static void on_ziti_event(ziti_context _ztx, const ziti_event_t *event);


/* config directives for ngx_http_ziti module */
static ngx_command_t ngx_http_ziti_cmds[] = {

    { ngx_string("ziti_pass"),
      NGX_HTTP_LOC_CONF|NGX_CONF_1MORE,
      ngx_http_ziti_pass,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    // { ngx_string("ziti_service"),
    //   NGX_HTTP_LOC_CONF|NGX_CONF_1MORE,
    //   ngx_http_ziti_service,
    //   NGX_HTTP_SRV_CONF_OFFSET,
    //   0,
    //   NULL },

    { ngx_string("ziti_identity"),
      NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF|NGX_CONF_TAKE1,
      ngx_http_ziti_identity,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

	// { ngx_string("ziti_thread_pool"),
	//   NGX_HTTP_LOC_CONF | NGX_CONF_NOARGS | NGX_CONF_TAKE1,
	//   ngx_http_ziti_thread_pool,
	//   NGX_HTTP_LOC_CONF_OFFSET,
	//   offsetof(ngx_http_ziti_loc_conf_t, thread_pool),
	//   NULL },

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


/**
 * 
 */
static ngx_int_t
ngx_http_ziti_preconfiguration(ngx_conf_t *cf)
{
    ngx_thread_pool_t          *tp;

    dd("entered, cf: %p", cf);

    tp = ngx_thread_pool_add(cf, &ngx_http_ziti_thread_pool_name);
    dd("thread_pool is: %p", tp);

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

    dd("entered, zlcf is: %p", zlcf);

    if (zlcf->servicename != NULL) {
        return "is duplicate";
    }

    if (ngx_conf_str_set(cf, &servicename, &value[1], "ziti_pass", &cmd->name, 1))
    {
        return NGX_CONF_ERROR;
    }

    dd("zlcf->servicename is: %s", servicename.sv.data);

    zlcf->servicename = strdup((char*)servicename.sv.data);  

    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);

    clcf->handler = ngx_http_ziti_handler;

    return NGX_CONF_OK;
}



static char *
ngx_http_ziti_identity(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_ziti_loc_conf_t   *zlcf = conf;
    ngx_str_t                  *value = cf->args->elts;
    ngx_conf_str_t              identity_path;
    // ngx_http_ziti_uv_run_thread_ctx_t *ctx;
    // ngx_thread_task_t          *task;

    dd("entered, zlcf is: %p", zlcf);

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

    dd("zlcf->identity_path is: %s", zlcf->identity_path);

    return NGX_CONF_OK;
}


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


static ngx_int_t ngx_http_ziti_get_buf(ngx_http_request_t *r, ngx_http_ziti_request_ctx_t *request_ctx, ssize_t len, ngx_buf_t **out_buf);

typedef struct {
    char          *name;
    uint32_t       key;
} ngx_http_ziti_method_t;

ngx_http_ziti_method_t ngx_http_ziti_methods[] = {
    { (char *) "GET",       (uint32_t) NGX_HTTP_GET },
    { (char *) "HEAD",      (uint32_t) NGX_HTTP_HEAD },
    { (char *) "POST",      (uint32_t) NGX_HTTP_POST },
    { (char *) "PUT",       (uint32_t) NGX_HTTP_PUT },
    { (char *) "DELETE",    (uint32_t) NGX_HTTP_DELETE },
    { (char *) "MKCOL",     (uint32_t) NGX_HTTP_MKCOL },
    { (char *) "COPY",      (uint32_t) NGX_HTTP_COPY },
    { (char *) "MOVE",      (uint32_t) NGX_HTTP_MOVE },
    { (char *) "OPTIONS",   (uint32_t) NGX_HTTP_OPTIONS },
    { (char *) "PROPFIND" , (uint32_t) NGX_HTTP_PROPFIND },
    { (char *) "PROPPATCH", (uint32_t) NGX_HTTP_PROPPATCH },
    { (char *) "LOCK",      (uint32_t) NGX_HTTP_LOCK },
    { (char *) "UNLOCK",    (uint32_t) NGX_HTTP_UNLOCK },
    { (char *) "PATCH",     (uint32_t) NGX_HTTP_PATCH },
    { NULL, 0 }
};

/**
 *  Number of different Ziti services we can have client pools for
 */
enum { listMapCapacity = 1000 };


struct ListMap* HttpsClientListMap;

struct key_value {
    char* key;
    void* value;
};

struct ListMap {
    struct   key_value kvPairs[listMapCapacity];
    size_t   count;
    uv_sem_t sem;
};

struct ListMap* newListMap(ngx_http_request_t *r) {
    struct ListMap* listMap = ngx_calloc(sizeof *listMap, r->connection->log);
    return listMap;
}

uv_mutex_t client_pool_lock;

bool listMapInsert(struct ListMap* collection, ngx_http_ziti_request_ctx_t *request_ctx, void* value) 
{
    ngx_http_request_t          *r = request_ctx->r;
    ngx_http_ziti_loc_conf_t    *zlcf = ngx_http_get_module_loc_conf(r, ngx_http_ziti_module);

    if (collection->count == zlcf->client_pool_size) {
        ngx_log_error(NGX_LOG_ALERT, r->connection->log, 0, "max services already at capacity [%d], insert FAIL", zlcf->client_pool_size);
        return false;
    }
    
    collection->kvPairs[collection->count].key = strdup(request_ctx->scheme_host_port);
    collection->kvPairs[collection->count].value = value;
    collection->count++;

    return true;
}


struct ListMap* getInnerListMapValueForKey(struct ListMap* collection, char* key) {
    struct ListMap* value = NULL;

    for (size_t i = 0 ; i < collection->count && value == NULL ; ++i) {
        if (strcmp(collection->kvPairs[i].key, key) == 0) {
            value = collection->kvPairs[i].value;
        }
    }
    return value;
}


HttpsClient* getHttpsClientForKey(struct ListMap* collection, char* key, ngx_http_request_t *r)  
{
    ngx_http_ziti_loc_conf_t    *zlcf = ngx_http_get_module_loc_conf(r, ngx_http_ziti_module);

    HttpsClient* value = NULL;
    size_t busyCount = 0;
    for (size_t i = 0 ; i < collection->count && value == NULL ; ++i) {
        if (strcmp(collection->kvPairs[i].key, key) == 0) {
          value = collection->kvPairs[i].value;
            if (value->active) {      // if it's in use
                value = NULL;         //  then keep searching
            }
            else if (value->purge) {  // if it's broken
                value = NULL;         //  then keep searching
            } else {
                value->active = true; // mark the one we will return as 'in use'
            }
            busyCount++;
        }
    }
    ngx_log_debug3(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "returning value '%p', collection->count is: [%d], busy-count is: [%d]", value, collection->count, busyCount);

    if (busyCount == zlcf->client_pool_size) {
        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "All available clients [%d] now in use; additional requests will be queued until clients are returned to pool", busyCount);
    }

    return value;
}


void freeListMap(struct ListMap* collection) {
    if (collection == NULL) {
        return;
    }
    for (size_t i = 0 ; i < collection->count ; ++i) {
        free(collection->kvPairs[i].value);
    }
    free(collection);
}


/**
 * 
 */
static int purge_and_replace_bad_clients(struct ListMap* clientListMap, ngx_http_ziti_request_ctx_t *request_ctx) 
{
    ngx_http_request_t          *r = request_ctx->r;
    ngx_http_ziti_loc_conf_t    *zlcf = ngx_http_get_module_loc_conf(r, ngx_http_ziti_module);

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "purge_and_replace_bad_clients() entered");

    int numReplaced = 0;
    for (size_t i = 0; i < zlcf->client_pool_size; i++) {

        HttpsClient* httpsClient = clientListMap->kvPairs[i].value;

        ZITI_LOG(DEBUG, "httpsClient is: %p", httpsClient);

        if (httpsClient->purge) {

            ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "*********** purging client [%p] from slot [%d]", httpsClient, i);

            httpsClient = ngx_calloc(sizeof *httpsClient, r->connection->log);

            httpsClient->scheme_host_port = strdup(request_ctx->scheme_host_port);
            ziti_src_init(zlcf->uv_thread_loop, &(httpsClient->ziti_src), zlcf->servicename, zlcf->ztx);
            um_http_init_with_src(zlcf->uv_thread_loop, &(httpsClient->client), request_ctx->scheme_host_port, (um_src_t *)&(httpsClient->ziti_src) );

            clientListMap->kvPairs[i].value = httpsClient;

            ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "*********** new client [%p] now occupying slot [%d]", httpsClient, i);

            numReplaced++;
        }
    }
    return numReplaced;
}


/**
 * 
 */
static void allocate_client(uv_work_t* req) 
{
    ngx_http_ziti_request_ctx_t *request_ctx = (ngx_http_ziti_request_ctx_t*)req->data;
    ngx_http_request_t          *r = request_ctx->r;
    ngx_http_ziti_loc_conf_t    *zlcf = ngx_http_get_module_loc_conf(r, ngx_http_ziti_module);

    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "allocate_client() entered, uv_work_t is: %p, request_ctx is: %p", req, request_ctx);

    HttpsReq* httpsReq = (HttpsReq*) ngx_pcalloc(request_ctx->pool, sizeof(HttpsReq));

    request_ctx->httpsReq = httpsReq;
    httpsReq->request_ctx = request_ctx;

    struct ListMap* clientListMap = getInnerListMapValueForKey(HttpsClientListMap, request_ctx->scheme_host_port);

    if (NULL == clientListMap) { // If first time seeing this key, spawn a pool of clients for it

        clientListMap = newListMap(r);
        listMapInsert(HttpsClientListMap, request_ctx, (void*)clientListMap);

        uv_sem_init(&(clientListMap->sem), zlcf->client_pool_size);

        for (size_t i = 0; i < zlcf->client_pool_size; i++) {

            HttpsClient* httpsClient = ngx_calloc(sizeof *httpsClient, r->connection->log);
            httpsClient->scheme_host_port = strdup(request_ctx->scheme_host_port);
            ziti_src_init(zlcf->uv_thread_loop, &(httpsClient->ziti_src), zlcf->servicename, zlcf->ztx );
            um_http_init_with_src(zlcf->uv_thread_loop, &(httpsClient->client), request_ctx->scheme_host_port, (um_src_t *)&(httpsClient->ziti_src) );

            listMapInsert(clientListMap, request_ctx, (void*)httpsClient);

        }
    }

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "----------> acquiring sem");
    uv_sem_wait(&(clientListMap->sem));
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "----------> successfully acquired sem");

    request_ctx->httpsClient = getHttpsClientForKey(clientListMap, request_ctx->scheme_host_port, r);
    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "----------> client is: [%p]", request_ctx->httpsClient);

    if (NULL == request_ctx->httpsClient) {
        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "----------> client is NULL, so we must purge_and_replace_bad_clients");
        purge_and_replace_bad_clients(clientListMap, request_ctx);

        request_ctx->httpsClient = getHttpsClientForKey(clientListMap, request_ctx->scheme_host_port, r);
        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "----------> client is: [%p]", request_ctx->httpsClient);
    }

    if (NULL == request_ctx->httpsClient) {
        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "----------> client is NULL, so we are in an unrecoverable state!");
    }
}


/**
 * 
 */
void propagate_headers_to_request(um_http_req_t *ur, ngx_http_request_t *r) {
    ngx_list_part_t            *part;
    ngx_table_elt_t            *h;
    ngx_uint_t                  i;

    // Get the first part of the list. There is usual only one part.
    part = &r->headers_in.headers.part;
    h = part->elts;

    // Headers list array may consist of more than one part, so loop through all of it
    for (i = 0; /* void */ ; i++) {
        if (i >= part->nelts) {
            if (part->next == NULL) {
                /* The last part, we're done. */
                break;
            }

            part = part->next;
            h = part->elts;
            i = 0;
        }

        um_http_req_header(ur, (char*)h[i].key.data, (char*)h[i].value.data);
        
        ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "added header to um_http_req_t: '%s:%s'", h[i].key.data, h[i].value.data);
    }
}


static void
ngx_http_ziti_req_complete_func(void *data, ngx_log_t *log)
{
    /* this function is executed in a thread from the ziti thread_pool */

    //
    // This function is a nop.
    // It's only purpose is to terminate and cause the
    //  ngx_http_ziti_req_thread_completion func to run over on the Nginx thread loop
    //
}


static void
ngx_http_ziti_req_thread_completion(ngx_event_t *ev)
{
    ngx_http_ziti_req_complete_thread_ctx_t *req_complete_thread_ctx = ev->data;
    ngx_http_request_t                      *r = req_complete_thread_ctx->r;

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "ngx_http_ziti_req_thread_completion() entered, r: %p", r);

    /* this function is executed in nginx event loop */

    // Cause a re-drive of our main request handler so it can transmit the now-completed response
    r->blocked--;
    r->aio = 0;

    req_complete_thread_ctx->request_ctx->state = ZS_RESP_BODY_DONE;

    ngx_http_handler(r);
}


/**
 * 
 */
static ngx_int_t
ngx_http_ziti_get_buf(ngx_http_request_t *r, ngx_http_ziti_request_ctx_t *request_ctx, ssize_t len, ngx_buf_t **out_buf)
{
    ngx_buf_t *buf;

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "ngx_http_ziti_get_buf() entered, len: %d", len);

    buf = ngx_create_temp_buf(request_ctx->pool, len);

    if (buf == NULL) {
        return NGX_ERROR;
    }

    buf->tag = (ngx_buf_tag_t) &ngx_http_ziti_module;
    buf->recycled = 1;

    *out_buf = buf;

    return NGX_OK;
}


/**
 * 
 */
static ngx_int_t
ngx_http_ziti_submit_mem(ngx_http_request_t *r, ngx_http_ziti_request_ctx_t *request_ctx, ngx_buf_t *out_buf)
{
    ngx_chain_t     *cl;
    ngx_chain_t     *existing_cl;

    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "ngx_http_ziti_submit_mem() entered, r: %p, len: %d", r, (int)(out_buf->last - out_buf->pos));


    if (request_ctx->out_bufs == NULL) {    // if no queued bufs

        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "ngx_http_ziti_submit_mem() allocating new chain of out_bufs");

        cl = ngx_alloc_chain_link(request_ctx->pool);
        if (cl == NULL) {
            goto failed;
        }

        cl->buf = out_buf;
        cl->next = NULL;

        out_buf->last_buf = 1;

        request_ctx->out_bufs = cl;

        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "ngx_http_ziti_submit_mem() attaching out_buf to new chain");

    } else {

        cl = ngx_alloc_chain_link(request_ctx->pool);
        if (cl == NULL) {
            goto failed;
        }

        cl->buf = out_buf;
        cl->next = NULL;

        out_buf->last_buf = 1;

        existing_cl = request_ctx->out_bufs;

        while (existing_cl->next) {
            existing_cl = existing_cl->next;
        }

        existing_cl->next = cl;
        existing_cl->buf->last_buf = 0;

        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "ngx_http_ziti_submit_mem() attaching out_buf to existing chain");
    }

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "ngx_http_ziti_submit_mem() exiting");


    return NGX_OK;

failed:

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "ngx_http_ziti_submit_mem() exiting with ERROR");


    return NGX_ERROR;
}


static void
ngx_http_ziti_resp_chunk_func(void *data, ngx_log_t *log)
{
    /* this function is executed in a thread from the ziti thread_pool */

    //
    // This function is a nop.
    // It's only purpose is to terminate and cause the
    //  ngx_http_ziti_resp_header_transmit_thread_completion func to run over on the Nginx thread loop
    //
}


static void
ngx_http_ziti_resp_chunk_thread_completion(ngx_event_t *ev)
{
    ngx_http_ziti_resp_chunk_thread_ctx_t *resp_chunk_thread_ctx = ev->data;
    ngx_http_request_t                      *r = resp_chunk_thread_ctx->r;

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "ngx_http_ziti_resp_chunk_thread_completion() entered, r: %p", r);

    /* this function is executed in nginx event loop */

    resp_chunk_thread_ctx->request_ctx->state = ZS_RESP_BODY_CHUNK_TRANSMIT_REQUIRED;

    ngx_http_handler(r);
}


/**
 * 
 */
void 
on_resp_body(um_http_req_t *req, const char *body, ssize_t len) 
{
    ngx_http_ziti_request_ctx_t *request_ctx = (ngx_http_ziti_request_ctx_t*)req->data;
    ngx_http_request_t          *r = request_ctx->r;
    ngx_http_ziti_loc_conf_t    *zlcf = ngx_http_get_module_loc_conf(r, ngx_http_ziti_module);
    ngx_http_ziti_resp_chunk_thread_ctx_t *resp_chunk_thread_ctx;
    ngx_thread_task_t           *task_RespChunk;
    ngx_http_ziti_req_complete_thread_ctx_t  *req_complete_thread_ctx;
    ngx_thread_task_t           *task_ReqComplete;
    ngx_thread_pool_t           *tp;
    ngx_buf_t                   *out_buf;
    ngx_int_t                    rc;

    ngx_log_debug3(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "on_resp_body() entered, body: %p, len: %d, httpsClient: %p", body, len, request_ctx->httpsClient);

    if (NULL != body) 
    {
        /* acquire lock */
        uv_sem_wait(&(request_ctx->out_bufs_sem));

        /* alloc buffer */
        rc = ngx_http_ziti_get_buf(r, request_ctx, len, &out_buf);
        if (rc != NGX_OK) {
            ZITI_LOG(ERROR, "ngx_http_ziti_get_buf() returned %d", rc);
            return;
        }

        /* fill in the buffer */
        out_buf->last = ngx_copy(out_buf->start, body, (uint32_t) len);

        if ((ssize_t) (out_buf->last - out_buf->pos) != len) {
            ZITI_LOG(DEBUG, "len %d", (int) len);

            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "ziti: FATAL: output on_resp_body buffer error");
            return;
        }

        /* queue buffer for transmit */
        ngx_http_ziti_submit_mem(r, request_ctx, out_buf);

        /* release lock */
        uv_sem_post(&(request_ctx->out_bufs_sem));

        // ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "on_resp_body() body: %s", body);

        //
        // Launch thread that will kick the Nginx threadloop
        //
        task_RespChunk = ngx_thread_task_alloc(zlcf->pool, sizeof(ngx_http_ziti_resp_chunk_thread_ctx_t));
        if (task_RespChunk == NULL) {
            ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "on_resp_body: ngx_thread_task_alloc failed");
            return;
        }
        resp_chunk_thread_ctx = task_RespChunk->ctx;
        resp_chunk_thread_ctx->r = r;
        resp_chunk_thread_ctx->request_ctx = request_ctx;
        resp_chunk_thread_ctx->zlcf = zlcf;

        task_RespChunk->handler = ngx_http_ziti_resp_chunk_func;
        task_RespChunk->event.handler = ngx_http_ziti_resp_chunk_thread_completion;
        task_RespChunk->event.data = resp_chunk_thread_ctx;

        tp = ngx_thread_pool_get((ngx_cycle_t* ) ngx_cycle, &ngx_http_ziti_thread_pool_name);
        if (tp == NULL) {
            ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "on_resp_body: ngx_thread_pool_get failed");
            return;
        }

        if (ngx_thread_task_post(tp, task_RespChunk) != NGX_OK) {
            ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "on_resp_body: ngx_thread_task_post failed");
            return;
        }

        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "on_resp_body: started thread for ngx_http_ziti_resp_chunk_func()");

    }

    else if ((NULL == body) && (UV_EOF == len)) 
    {
        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "<--------- returning httpsClient [%p] back to pool", request_ctx->httpsClient);
        request_ctx->httpsClient->active = false;

        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "request_ctx->scheme_host_port: [%s] ", request_ctx->scheme_host_port);
        struct ListMap* clientListMap = getInnerListMapValueForKey(HttpsClientListMap, request_ctx->scheme_host_port);
        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "clientListMap: [%p] ", clientListMap);

        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "<-------- returning sem for client: [%p] ", request_ctx->httpsClient);
        uv_sem_post(&(clientListMap->sem));
        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "          after returning sem for client: [%p] ", request_ctx->httpsClient);

        //
        // Launch thread that will kick the Nginx threadloop
        //
        task_ReqComplete = ngx_thread_task_alloc(zlcf->pool, sizeof(ngx_http_ziti_req_complete_thread_ctx_t));
        if (task_ReqComplete == NULL) {
            ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "on_resp_body: ngx_thread_task_alloc failed");
            return;
        }
        req_complete_thread_ctx = task_ReqComplete->ctx;
        req_complete_thread_ctx->r = r;
        req_complete_thread_ctx->request_ctx = request_ctx;
        req_complete_thread_ctx->zlcf = zlcf;

        task_ReqComplete->handler = ngx_http_ziti_req_complete_func;
        task_ReqComplete->event.handler = ngx_http_ziti_req_thread_completion;
        task_ReqComplete->event.data = req_complete_thread_ctx;

        tp = ngx_thread_pool_get((ngx_cycle_t* ) ngx_cycle, &ngx_http_ziti_thread_pool_name);
        if (tp == NULL) {
            ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "on_resp_body: ngx_thread_pool_get failed");
            return;
        }

        if (ngx_thread_task_post(tp, task_ReqComplete) != NGX_OK) {
            ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "on_resp_body: ngx_thread_task_post failed");
            return;
        }

        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "on_resp_body: started thread for ngx_http_ziti_req_complete_func()");
    }

}


/**
 * 
 */
ngx_int_t
ngx_http_ziti_set_header(ngx_http_request_t *r, ngx_str_t *key, ngx_str_t *value)
{
    ngx_uint_t                   i;
    ngx_table_elt_t             *h;
    ngx_list_part_t             *part;

    // ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "ngx_http_ziti_set_header() entered for '%s: %s'", key->data, value->data);

    if (ngx_strncasecmp(key->data, (u_char*)"Content-Length", key->len) == 0) {
        r->headers_out.content_length_n = ngx_atoi(value->data, value->len);
        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "ngx_http_ziti_set_header() setting content_length_n: %d", r->headers_out.content_length_n);
        return NGX_OK;
    }
    else 
    if (ngx_strncasecmp(key->data, (u_char*)"Content-Type", key->len) == 0) {
        r->headers_out.content_type_len = value->len;
        r->headers_out.content_type = *value;
        return NGX_OK;
    }

    part = &r->headers_out.headers.part;
    h = part->elts;

    for (i = 0; /* void */; i++) 
    {
        if (i >= part->nelts) {
            if (part->next == NULL) {
                break;
            }

            part = part->next;
            h = part->elts;
            i = 0;
        }

        if (h[i].key.len == key->len && ngx_strncasecmp(h[i].key.data, key->data, h[i].key.len) == 0)
        {
            if (value->len == 0) {
                h[i].hash = 0;
            }

            h[i].value = *value;

            ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "ngx_http_ziti_set_header() updating '%s: %s'", h->key.data, h->value.data);

            return NGX_OK;
        }
    }

    if (value->len == 0) {
        return NGX_OK;
    }

    h = ngx_list_push(&r->headers_out.headers);

    if (h == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    h->hash = 1;
    h->key = *key;
    h->value = *value;

    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "ngx_http_ziti_set_header() adding '%s: %s'", h->key.data, h->value.data);

    return NGX_OK;
}


static void
ngx_http_ziti_resp_header_transmit_func(void *data, ngx_log_t *log)
{
    /* this function is executed in a thread from the ziti thread_pool */

    //
    // This function is a nop.
    // It's only purpose is to terminate and cause the
    //  ngx_http_ziti_resp_header_transmit_thread_completion func to run over on the Nginx thread loop
    //
}


static void
ngx_http_ziti_resp_header_transmit_thread_completion(ngx_event_t *ev)
{
    ngx_http_ziti_resp_header_transmit_thread_ctx_t *resp_header_transmit_thread_ctx = ev->data;
    ngx_http_request_t                      *r = resp_header_transmit_thread_ctx->r;

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "ngx_http_ziti_resp_header_transmit_thread_completion() entered, r: %p", r);

    /* this function is executed in nginx event loop */

    resp_header_transmit_thread_ctx->request_ctx->state = ZS_RESP_HEADER_TRANSMIT_REQUIRED;

    ngx_http_handler(r);
}


/**
 * 
 */
void 
on_resp(um_http_resp_t *resp, void *data) 
{
    ngx_http_ziti_request_ctx_t *request_ctx = (ngx_http_ziti_request_ctx_t*)data;
    ngx_http_request_t          *r = request_ctx->r;
    ngx_http_ziti_loc_conf_t    *zlcf = ngx_http_get_module_loc_conf(r, ngx_http_ziti_module);
    ngx_str_t                    key, value;
    ngx_http_ziti_resp_header_transmit_thread_ctx_t *resp_header_transmit_thread_ctx;
    ngx_thread_task_t           *task_transmitRespHeader;
    ngx_thread_pool_t           *tp;

    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "on_resp() entered for resp: %p, httpsReq: %p", resp, request_ctx->httpsReq);

    // status code
    r->headers_out.status = resp->code;

    // headers
    um_http_hdr *h;
    LIST_FOREACH(h, &resp->headers, _next) {
        key.data = (u_char *) strdup(h->name);
        key.len  = strlen(h->name);
        value.data = (u_char *) strdup(h->value);
        value.len = strlen(h->value);
        ngx_http_ziti_set_header(r, &key, &value);
    }

    if ((UV_EOF == resp->code) || (resp->code < 0)) {
        ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "<--------- returning httpsClient [%p] back to pool due to error: [%d]", request_ctx->httpsClient, resp->code);
        request_ctx->httpsClient->active = false;

        // Before we fully release this client (via uv_sem_post) let's indicate purge is needed, because after errs happen on a client, 
        // subsequent requests using that client never get processed.
        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "*********** due to error, purge now necessary for client: [%p]", request_ctx->httpsClient);

        request_ctx->httpsClient->purge = true;

        struct ListMap* clientListMap = getInnerListMapValueForKey(HttpsClientListMap, request_ctx->scheme_host_port);

        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "<-------- returning sem for client: [%p] ", request_ctx->httpsClient);
        uv_sem_post(&(clientListMap->sem));
        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "          after returning sem for client: [%p] ", request_ctx->httpsClient);
    }

    if (UV_EOF != resp->code) {
        // We need body of the HTTP response, so wire up that callback now
        resp->body_cb = on_resp_body;
    }

    //
    // Launch thread that will kick the Nginx threadloop
    //
    task_transmitRespHeader = ngx_thread_task_alloc(zlcf->pool, sizeof(ngx_http_ziti_resp_header_transmit_thread_ctx_t));
    if (task_transmitRespHeader == NULL) {
        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "on_resp: ngx_thread_task_alloc failed");
        return;
    }
    resp_header_transmit_thread_ctx = task_transmitRespHeader->ctx;
    resp_header_transmit_thread_ctx->r = r;
    resp_header_transmit_thread_ctx->request_ctx = request_ctx;
    resp_header_transmit_thread_ctx->zlcf = zlcf;

    task_transmitRespHeader->handler = ngx_http_ziti_resp_header_transmit_func;
    task_transmitRespHeader->event.handler = ngx_http_ziti_resp_header_transmit_thread_completion;
    task_transmitRespHeader->event.data = resp_header_transmit_thread_ctx;

    tp = ngx_thread_pool_get((ngx_cycle_t* ) ngx_cycle, &ngx_http_ziti_thread_pool_name);
    if (tp == NULL) {
        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "on_resp: ngx_thread_pool_get failed");
        return;
    }

    if (ngx_thread_task_post(tp, task_transmitRespHeader) != NGX_OK) {
        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "on_resp: ngx_thread_task_post failed");
        return;
    }

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "on_resp: started thread for ngx_http_ziti_resp_header_transmit_func()");
}


/**
 * 
 */
void
on_req_body(um_http_req_t *req, const char *body, ssize_t status) 
{
    /* nop */
}


/**
 * 
 */
void
on_client_data(ngx_http_request_t *r)
{
    ngx_http_ziti_request_ctx_t   *request_ctx;
    off_t                          len;
    ngx_chain_t                   *in;
    um_http_req_t                 *ur;

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "on_client_data() entered");

    if (r->request_body == NULL) {
        return;
    }

    request_ctx = (ngx_http_ziti_request_ctx_t*)ngx_http_get_module_ctx(r, ngx_http_ziti_module);

    ur = request_ctx->httpsReq->req;

    for (in = r->request_body->bufs; in; in = in->next) 
    {
        len = ngx_buf_size(in->buf);

        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "on_client_data() chunk len is: %d", len);

        // write the data over to the Ziti service
        um_http_req_data(ur, (void*)in->buf->start, len, on_req_body );
    }
}


/**
 * 
 */
void on_client(uv_work_t* req, int status) 
{
    ngx_http_ziti_request_ctx_t *request_ctx = (ngx_http_ziti_request_ctx_t*)req->data;
    ngx_http_request_t          *r = request_ctx->r;
    ngx_http_ziti_method_t      *method;

    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "on_client() entered, uv_work_t is: %p, status is: %d", req, status);
    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "client is: [%p]", request_ctx->httpsClient);

    for (method = ngx_http_ziti_methods; method->name; method++) {
        ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "method->key is: [%d], method->name is: [%s]", method->key, method->name);
        if (r->method == method->key) {
            ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "method FOUND [%s]", method->name);
            break;
        }
    }
    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "method->name is: [%s]", method->name);

    char * uri_path = (char*) ngx_pcalloc(request_ctx->pool, r->uri.len + 1);
    ngx_copy(uri_path, r->uri.data, r->uri.len);
    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "uri_path  is: [%s]", uri_path);

    // Initiate the request:   HTTP -> TLS -> Ziti -> Service 
    um_http_req_t *ur = um_http_req(
        &(request_ctx->httpsClient->client),
        method->name,
        uri_path,
        on_resp,
        request_ctx  /* Pass our request_ctx around so we can eventually mark it complete */
    );

    request_ctx->state = ZS_REQ_PROCESSING;

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "um_http_req_t: %p", ur);

    request_ctx->httpsReq->req = ur;

    // Add headers to request
    propagate_headers_to_request(ur, r);

    // Ask Nginx for the client (POST|PUT) data
    ngx_http_read_client_request_body(r, on_client_data);

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "on_client() exiting");
}





static void 
nop(uv_async_t *handle, int status) { }

static void 
uv_thread_loop_func(void *data){
    uv_loop_t *thread_loop = (uv_loop_t *) data;

    //Start the loop
    uv_run(thread_loop, UV_RUN_DEFAULT);
}

static void
ngx_http_ziti_await_init_complete_func(void *data, ngx_log_t *log)
{
    /* this function is executed in a thread from the ziti thread_pool */

    ngx_http_ziti_await_init_thread_ctx_t   *ctx = data;
    ngx_http_ziti_loc_conf_t                *zlcf = ctx->zlcf;
    ngx_uint_t                               msec_sleep = 100;

    ZITI_LOG(DEBUG, "ngx_http_ziti_await_init_complete_func() entered");

    do {
        ZITI_LOG(DEBUG, "ngx_http_ziti_await_init_complete_func() sleeping");
        ngx_msleep(msec_sleep);
    } while (zlcf->state < ZS_LOC_ZITI_INIT_COMPLETED);

    ZITI_LOG(DEBUG, "ngx_http_ziti_await_init_complete_func() exiting");
}

static void
ngx_http_ziti_await_init_complete_completion(ngx_event_t *ev)
{
    /* this function is executed in nginx event loop */

    ngx_http_ziti_await_init_thread_ctx_t *ctx;
    ngx_http_request_t *r;

    dd("entered");

    ctx = ev->data;
    r = ctx->r;

    r->blocked--;
    r->aio = 0;

    ngx_http_handler(r);
}

ngx_int_t
ngx_http_ziti_await_init(ngx_http_ziti_loc_conf_t *zlcf, ngx_http_request_t *r)
{
    ngx_thread_pool_t             *tp;

    ngx_http_ziti_await_init_thread_ctx_t *await_init_thread_ctx;
    ngx_thread_task_t             *task_awaitInit;

    //
    // Launch thread to await completion of Ziti init (Controller connection)
    //
    task_awaitInit = ngx_thread_task_alloc(zlcf->pool, sizeof(ngx_http_ziti_await_init_thread_ctx_t));
    if (task_awaitInit == NULL) {
        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "ngx_http_ziti_await_init: ngx_thread_task_alloc failed");
        return NGX_ERROR;
    }

    await_init_thread_ctx = task_awaitInit->ctx;
    await_init_thread_ctx->r = r;
    await_init_thread_ctx->zlcf = zlcf;

    task_awaitInit->handler = ngx_http_ziti_await_init_complete_func;
    task_awaitInit->event.handler = ngx_http_ziti_await_init_complete_completion;
    task_awaitInit->event.data = await_init_thread_ctx;

    tp = ngx_thread_pool_get((ngx_cycle_t* ) ngx_cycle, &ngx_http_ziti_thread_pool_name);
    if (tp == NULL) {
        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "ngx_http_ziti_await_init: ngx_thread_pool_get failed");
        return NGX_ERROR;
    }

    if (ngx_thread_task_post(tp, task_awaitInit) != NGX_OK) {
        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "ngx_http_ziti_await_init: ngx_thread_task_post failed");
        return NGX_ERROR;
    }

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "ngx_http_ziti_await_init: Exiting");

    return NGX_OK;
}


/**
 * 
 */
ngx_int_t
ngx_http_ziti_handler(ngx_http_request_t *r)
{
    ngx_http_ziti_loc_conf_t      *zlcf;
    ngx_http_ziti_request_ctx_t   *request_ctx;
    ngx_int_t                      rc;

    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "ngx_http_ziti_handler: Entering handler, r->count: %d, r->blocked: %d", r->count, r->blocked);

    zlcf = ngx_http_get_module_loc_conf(r, ngx_http_ziti_module);

    //
    // Obtain context for this request
    //
    request_ctx = (ngx_http_ziti_request_ctx_t*)ngx_http_get_module_ctx(r, ngx_http_ziti_module);
    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "ngx_http_ziti_request_ctx_t is: %p", request_ctx);
    if(request_ctx == NULL)
    {
        //
        // First time through for this request, so create the request context
        //
        request_ctx = (ngx_http_ziti_request_ctx_t*) ngx_pcalloc(r->pool, sizeof(ngx_http_ziti_request_ctx_t));
        if (request_ctx == NULL) 
        {
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }
        ngx_http_set_ctx(r, request_ctx, ngx_http_ziti_module);

        request_ctx->r = r;

        request_ctx->pool = ngx_create_pool(NGX_DEFAULT_POOL_SIZE, r->connection->log);
        if (request_ctx->pool == NULL) {
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }

        request_ctx->scheme_host_port = "http://example:80"; // keep um_http_init_with_src() happy

        if (NULL == HttpsClientListMap) {
            HttpsClientListMap = newListMap(r);
        }

        uv_sem_init(&(request_ctx->out_bufs_sem), 1);

        request_ctx->last_out = &request_ctx->out_bufs;
    }

    //
    // Await the Ziti init if necessary
    //
    if (zlcf->state < ZS_LOC_UV_LOOP_STARTED) {

        rc = ngx_http_ziti_await_init(zlcf, r);

        if (rc != NGX_OK) {
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }

        r->main->blocked++;
        r->main->count++;
        r->aio = 1;

        return NGX_AGAIN;
    }

    //
    // If location-scoped Ziti initialization is not completed, exit and await next iteration
    //
    if (zlcf->state < ZS_LOC_ZITI_INIT_COMPLETED) {
        return NGX_AGAIN;
    }

    //
    // If we get this far, the location-scope is fully initialized, and we can now orchestrate the request over Ziti
    //
    if (request_ctx->state == ZS_REQ_INIT)  // If we haven't actually started the request yet
    {
        //
        // Queue the HTTP request.  First thing that happens in the flow is to allocate a client from the pool
        //
        request_ctx->uv_req.data = request_ctx;
        uv_queue_work(zlcf->uv_thread_loop, &request_ctx->uv_req, allocate_client, on_client);

        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "ngx_http_ziti_handler: uv_queue_work of allocate_client returned req: %p", request_ctx->uv_req);

        //
        // Mark the HTTP request as blocked/pending, and exit back to the Nginx loop
        //
        r->main->blocked++;
        r->main->count++;
        r->aio = 1;

        return NGX_AGAIN;
    }

    //
    // If we get this far, the response is starting to come back from Ziti, and we can send the response to the client
    //
    if (request_ctx->state == ZS_RESP_HEADER_TRANSMIT_REQUIRED)  // If we need to transmit the response header
    {
        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "ngx_http_ziti_handler: processing ZS_RESP_HEADER_TRANSMIT_REQUIRED ********************************************************");

        /* acquire lock */
        uv_sem_wait(&(request_ctx->out_bufs_sem));

        ngx_http_send_header(r); /* Send the headers */

        /* release lock */
        uv_sem_post(&(request_ctx->out_bufs_sem));

        r->main->count++;
        r->main->blocked--;

        return NGX_AGAIN;
    }

    //
    // If we get this far, the response body is starting to come back from Ziti, and we can send the response chunk to the client
    //
    if (request_ctx->state == ZS_RESP_BODY_CHUNK_TRANSMIT_REQUIRED)  // If we need to transmit the response body chunk
    {
        // ngx_chain_t     *cl;
        // ngx_chain_t     *clfree;

        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "ngx_http_ziti_handler: processing ZS_RESP_BODY_CHUNK_TRANSMIT_REQUIRED ********************************************************");

        /* acquire lock */
        uv_sem_wait(&(request_ctx->out_bufs_sem));

        /* Send the body chunk(s) */
        ngx_http_output_filter(r, request_ctx->out_bufs);

        request_ctx->out_bufs = NULL;

        /* release lock */
        uv_sem_post(&(request_ctx->out_bufs_sem));

        return NGX_AGAIN;
    }

    //
    // If we get this far, the response body is complete, and we can close up the response
    //

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "ngx_http_ziti_handler: Exiting handler <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<");

    /* Send any remaining body fragments, and return the status code of the output filter chain. */
    int outrc = ngx_http_output_filter(r, request_ctx->out_bufs);

    ngx_destroy_pool(request_ctx->pool);

    ngx_pfree(r->pool, request_ctx);

    return outrc;
}

#include <stdio.h>
#include <stdlib.h>

#include "proxy.h"

#include "vcc_if.h"

int
init_function(VRT_CTX, struct vmod_priv *priv, enum vcl_event_e e)
{
    if (e != VCL_EVENT_LOAD)
      return 0;

    proxy_init();

    return 0;
}

static struct proxy_request*
get_request(VRT_CTX, struct vmod_priv *priv, int alloc)
{
    struct proxy_request *req = (struct proxy_request *)priv->priv;
    CHECK_OBJ_ORNULL(req, PROXY_REQUEST_MAGIC);

    if (alloc) {
        AZ(req);
        req = proxy_create_request(ctx);
        CHECK_OBJ_NOTNULL(req, PROXY_REQUEST_MAGIC);
        priv->priv = (void *)req;
        priv->free = (void *)proxy_release_request;
    }

    return req;
}

VCL_VOID
vmod_call(VRT_CTX, struct vmod_priv *priv, VCL_BACKEND backend, VCL_STRING path)
{
    if (ctx->method != VCL_MET_RECV)
        return;

    int alloc = (ctx->req->esi_level == 0 && ctx->req->restarts == 0);
    struct proxy_request *req = get_request(ctx, priv, alloc);
    CHECK_OBJ_ORNULL(req, PROXY_REQUEST_MAGIC);

    // No req is valid if top-level VCL chose not create one
    if (!req && !alloc)
        return;

    // Restarted requests need a clean slate for curl below
    if (ctx->req->restarts != req->restarts)
        proxy_restart_request(ctx, req);

    // ESI requests reuse the same proxy headers
    // restarted requests regenerate the proxy headers
    if (ctx->req->esi_level == 0)
        proxy_curl(ctx, req, backend, path);

    proxy_process_request(ctx, req);
}

VCL_VOID
vmod_process(VRT_CTX, struct vmod_priv *priv)
{
    if (ctx->method != VCL_MET_DELIVER)
        return;

    struct proxy_request *req = get_request(ctx, priv, 0);
    CHECK_OBJ_ORNULL(req, PROXY_REQUEST_MAGIC);

    if (req)
        proxy_process_request(ctx, req);
}

VCL_STRING
vmod_error(VRT_CTX, struct vmod_priv *priv)
{
    if (ctx->method != VCL_MET_RECV)
        return NULL;

    struct proxy_request *req = get_request(ctx, priv, 0);
    CHECK_OBJ_ORNULL(req, PROXY_REQUEST_MAGIC);

    if (req)
        return req->error;

    return NULL;
}

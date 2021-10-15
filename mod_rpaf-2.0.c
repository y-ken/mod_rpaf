/*
   Copyright 2012 Kentaro YOSHIDA

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/

#include "httpd.h"
#include "http_config.h"
#include "http_core.h"
#include "http_log.h"
#include "http_protocol.h"
#include "http_vhost.h"
#include "apr_strings.h"
#include "arpa/inet.h"

module AP_MODULE_DECLARE_DATA rpaf_module;

typedef struct {
    int                enable;
    int                sethostname;
    int                sethttps;
    int                setport;
    const char         *headername;
    apr_array_header_t *proxy_ips;
    const char         *orig_scheme;
    const char         *https_scheme;
    int                orig_port;
} rpaf_server_cfg;

typedef struct {
    const char  *old_ip;
    int         old_family;
    request_rec *r;
} rpaf_cleanup_rec;

static void *rpaf_create_server_cfg(apr_pool_t *p, server_rec *s) {
    rpaf_server_cfg *cfg = (rpaf_server_cfg *)apr_pcalloc(p, sizeof(rpaf_server_cfg));
    if (!cfg)
        return NULL;

    cfg->proxy_ips = apr_array_make(p, 0, sizeof(char *));
    cfg->enable = 0;
    cfg->sethostname = 0;

    cfg->orig_scheme  = s->server_scheme;
    cfg->https_scheme = apr_pstrdup(p, "https");
    cfg->orig_port    = s->port;

    return (void *)cfg;
}

static const char *rpaf_set_proxy_ip(cmd_parms *cmd, void *dummy, const char *proxy_ip) {
    server_rec *s = cmd->server;
    rpaf_server_cfg *cfg = (rpaf_server_cfg *)ap_get_module_config(s->module_config, 
                                                                   &rpaf_module);

    /* check for valid syntax of ip */
    *(char **)apr_array_push(cfg->proxy_ips) = apr_pstrdup(cmd->pool, proxy_ip);
    return NULL;
}

static const char *rpaf_set_headername(cmd_parms *cmd, void *dummy, const char *headername) {
    server_rec *s = cmd->server;
    rpaf_server_cfg *cfg = (rpaf_server_cfg *)ap_get_module_config(s->module_config, 
                                                                   &rpaf_module);

    cfg->headername = headername; 
    return NULL;
}

static const char *rpaf_enable(cmd_parms *cmd, void *dummy, int flag) {
    server_rec *s = cmd->server;
    rpaf_server_cfg *cfg = (rpaf_server_cfg *)ap_get_module_config(s->module_config, 
                                                                   &rpaf_module);

    cfg->enable = flag;
    return NULL;
}

static const char *rpaf_sethostname(cmd_parms *cmd, void *dummy, int flag) {
    server_rec *s = cmd->server;
    rpaf_server_cfg *cfg = (rpaf_server_cfg *)ap_get_module_config(s->module_config, 
                                                                   &rpaf_module);

    cfg->sethostname = flag;
    return NULL;
}

static const char *rpaf_sethttps(cmd_parms *cmd, void *dummy, int flag) {
    server_rec *s = cmd->server;
    rpaf_server_cfg *cfg = (rpaf_server_cfg *)ap_get_module_config(s->module_config, 
                                                                   &rpaf_module);

    cfg->sethttps = flag;
    return NULL;
}

static const char *rpaf_setport(cmd_parms *cmd, void *dummy, int flag) {
    server_rec *s = cmd->server;
    rpaf_server_cfg *cfg = (rpaf_server_cfg *)ap_get_module_config(s->module_config, 
                                                                   &rpaf_module);

    cfg->setport = flag;
    return NULL;
}

static int is_in_array(const char *remote_ip, apr_array_header_t *proxy_ips) {
    int i;
    char **list = (char**)proxy_ips->elts;
    for (i = 0; i < proxy_ips->nelts; i++) {
        if (strncmp(remote_ip, list[i], strlen(list[i])) == 0)
            return 1;
    }
    return 0;
}

static apr_status_t rpaf_cleanup(void *data) {
    rpaf_cleanup_rec *rcr = (rpaf_cleanup_rec *)data;
    rcr->r->connection->remote_ip = apr_pstrdup(rcr->r->connection->pool, rcr->old_ip);
    rcr->r->connection->remote_addr->sa.sin.sin_addr.s_addr = apr_inet_addr(rcr->r->connection->remote_ip);
    rcr->r->connection->remote_addr->sa.sin.sin_family = rcr->old_family;
    return APR_SUCCESS;
}

static int change_remote_ip(request_rec *r) {
    const char *fwdvalue;
    char *val;
    rpaf_server_cfg *cfg = (rpaf_server_cfg *)ap_get_module_config(r->server->module_config,
                                                                   &rpaf_module);

    if (!cfg->enable)
        return DECLINED;

    if (is_in_array(r->connection->remote_ip, cfg->proxy_ips) == 1) {
        /* check if cfg->headername is set and if it is use
           that instead of X-Forwarded-For by default */
        if (cfg->headername && (fwdvalue = apr_table_get(r->headers_in, cfg->headername))) {
            //
        } else if ((fwdvalue = apr_table_get(r->headers_in, "X-Forwarded-For"))) {
            //
        } else {
            return DECLINED;
        }

        if (fwdvalue) {
            rpaf_cleanup_rec *rcr = (rpaf_cleanup_rec *)apr_pcalloc(r->pool, sizeof(rpaf_cleanup_rec));
            apr_array_header_t *arr = apr_array_make(r->pool, 0, sizeof(char*));
            while (*fwdvalue && (val = ap_get_token(r->pool, &fwdvalue, 1))) {
                *(char **)apr_array_push(arr) = apr_pstrdup(r->pool, val);
                if (*fwdvalue != '\0')
                    ++fwdvalue;
            }
            rcr->old_ip = apr_pstrdup(r->connection->pool, r->connection->remote_ip);
            rcr->old_family = r->connection->remote_addr->sa.sin.sin_family;
            rcr->r = r;
            apr_pool_cleanup_register(r->pool, (void *)rcr, rpaf_cleanup, apr_pool_cleanup_null);
            r->connection->remote_ip = apr_pstrdup(r->connection->pool, ((char **)arr->elts)[((arr->nelts)-1)]);
            r->connection->remote_addr->sa.sin.sin_addr.s_addr = apr_inet_addr(r->connection->remote_ip);
            r->connection->remote_addr->family = AF_INET;
            r->connection->remote_addr->sa.sin.sin_family = AF_INET;

            if (cfg->sethostname) {
                const char *hostvalue;
                if ((hostvalue = apr_table_get(r->headers_in, "X-Forwarded-Host"))) {
                    /* 2.0 proxy frontend or 1.3 => 1.3.25 proxy frontend */
                    apr_table_set(r->headers_in, "Host", apr_pstrdup(r->pool, hostvalue));
                    r->hostname = apr_pstrdup(r->pool, hostvalue);
                    ap_update_vhost_from_headers(r);
                } else if ((hostvalue = apr_table_get(r->headers_in, "X-Host"))) {
                    /* 1.3 proxy frontend with mod_proxy_add_forward */
                    apr_table_set(r->headers_in, "Host", apr_pstrdup(r->pool, hostvalue));
                    r->hostname = apr_pstrdup(r->pool, hostvalue);
                    ap_update_vhost_from_headers(r);
                }
            }
            
            if (cfg->sethttps) {
                const char *httpsvalue;
                if ((httpsvalue = apr_table_get(r->headers_in, "X-Forwarded-HTTPS")) ||
                    (httpsvalue = apr_table_get(r->headers_in, "X-HTTPS"))) {
                    apr_table_set(r->subprocess_env, "HTTPS", apr_pstrdup(r->pool, httpsvalue));
                    r->server->server_scheme = cfg->https_scheme;
                } else if ((httpsvalue = apr_table_get(r->headers_in, "X-Forwarded-Proto")) != NULL &&
                    (strcmp(httpsvalue, "https") == 0)) {
                    apr_table_set(r->subprocess_env, "HTTPS", apr_pstrdup(r->pool, "on"));
                    r->server->server_scheme = cfg->https_scheme;
                }
            }
            
            if (cfg->setport) {
                const char *portvalue;
                if ((portvalue = apr_table_get(r->headers_in, "X-Forwarded-Port")) ||
                    (portvalue = apr_table_get(r->headers_in, "X-Port"))) {
                    r->server->port    = atoi(portvalue);
                    r->parsed_uri.port = r->server->port;
                }
            }
        }
    }
    return DECLINED;
}

static const command_rec rpaf_cmds[] = {
    AP_INIT_FLAG(
                 "RPAFenable",
                 rpaf_enable,
                 NULL,
                 RSRC_CONF,
                 "Enable mod_rpaf"
                 ),
    AP_INIT_FLAG(
                 "RPAFsethostname",
                 rpaf_sethostname,
                 NULL,
                 RSRC_CONF,
                 "Let mod_rpaf set the hostname from the X-Forwarded-Host or X-Host header and update vhosts"
                 ),
    AP_INIT_FLAG(
                 "RPAFsethttps",
                 rpaf_sethttps,
                 NULL,
                 RSRC_CONF,
                 "Let mod_rpaf set the HTTPS environment variable from the X-HTTPS or X-Forwarded-HTTPS or X-Forwarded-Proto header"
                 ),
    AP_INIT_FLAG(
                 "RPAFsetport",
                 rpaf_setport,
                 NULL,
                 RSRC_CONF,
                 "Let mod_rpaf set the server port from the X-Port header"
                 ),
    AP_INIT_ITERATE(
                 "RPAFproxy_ips",
                 rpaf_set_proxy_ip,
                 NULL,
                 RSRC_CONF,
                 "IP(s) of Proxy server setting X-Forwarded-For header"
                 ),
    AP_INIT_TAKE1(
                 "RPAFheader",
                 rpaf_set_headername,
                 NULL,
                 RSRC_CONF,
                 "Which header to look for when trying to find the real ip of the client in a proxy setup"
                 ),
    { NULL }
};

static void register_hooks(apr_pool_t *p) {
    ap_hook_post_read_request(change_remote_ip, NULL, NULL, APR_HOOK_FIRST);
}

module AP_MODULE_DECLARE_DATA rpaf_module = {
    STANDARD20_MODULE_STUFF,
    NULL,
    NULL,
    rpaf_create_server_cfg,
    NULL,
    rpaf_cmds,
    register_hooks,
};

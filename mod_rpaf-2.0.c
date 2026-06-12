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
#include "apr_optional.h"
#include "apr_lib.h"
#include "arpa/inet.h"

/* Apache 2.4 removed conn_rec->remote_ip / remote_addr and exposes the client
   address on the request as useragent_ip / useragent_addr instead. Map to
   whichever the running version provides so the module builds and works on
   both 2.2 (CentOS 6) and 2.4 (CentOS 7). */
#if AP_SERVER_MAJORVERSION_NUMBER > 2 || \
    (AP_SERVER_MAJORVERSION_NUMBER == 2 && AP_SERVER_MINORVERSION_NUMBER >= 4)
  #define RPAF_IP   useragent_ip
  #define RPAF_ADDR useragent_addr
  #define RPAF_POOL pool
#else
  #define RPAF_IP   connection->remote_ip
  #define RPAF_ADDR connection->remote_addr
  #define RPAF_POOL connection->pool
#endif

module AP_MODULE_DECLARE_DATA rpaf_module;
APR_DECLARE_OPTIONAL_FN(int, ssl_is_https, (conn_rec *));

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

/* Cheap sanity check that a string looks like an IPv4 or IPv6 address, used
   to reject bogus X-Forwarded-For tokens before trusting them. */
static int rpaf_looks_like_ip(const char *ipstr) {
    static const char ipv4_set[] = "0123456789./";
    static const char ipv6_set[] = "0123456789abcdefABCDEF:./";
    const char *set = strchr(ipstr, ':') ? ipv6_set : ipv4_set;
    const char *ptr = ipstr;

    if (!*ipstr)
        return 0;
    while (*ptr && strchr(set, *ptr) != NULL)
        ++ptr;
    return (*ptr == '\0');
}

/* True when a forwarded HTTPS header value indicates an SSL request. Avoids
   trusting an arbitrary value (e.g. "off") as if it meant https. */
static int rpaf_https_is_on(const char *value) {
    return value && (strcasecmp(value, "on")    == 0 ||
                     strcasecmp(value, "1")      == 0 ||
                     strcasecmp(value, "true")   == 0 ||
                     strcasecmp(value, "yes")    == 0 ||
                     strcasecmp(value, "https")  == 0);
}

/* Return the last entry in the forwarded-for list that is not one of our
   trusted proxies -- i.e. the real client behind a chain of proxies -- or
   NULL if every entry is a trusted proxy (or the list is empty). */
static const char *last_not_in_array(apr_array_header_t *forwarded_for,
                                     apr_array_header_t *proxy_ips) {
    char **fwd_ips = (char **)forwarded_for->elts;
    int i;

    for (i = forwarded_for->nelts - 1; i >= 0; i--) {
        if (!is_in_array(fwd_ips[i], proxy_ips))
            return fwd_ips[i];
    }
    return NULL;
}

static apr_status_t rpaf_cleanup(void *data) {
    rpaf_cleanup_rec *rcr = (rpaf_cleanup_rec *)data;
    rcr->r->RPAF_IP = apr_pstrdup(rcr->r->RPAF_POOL, rcr->old_ip);
    rcr->r->RPAF_ADDR->sa.sin.sin_addr.s_addr = apr_inet_addr(rcr->r->RPAF_IP);
    rcr->r->RPAF_ADDR->sa.sin.sin_family = rcr->old_family;
    apr_table_unset(rcr->r->connection->notes, "rpaf_https");
    return APR_SUCCESS;
}

static int change_remote_ip(request_rec *r) {
    const char *fwdvalue;
    char *val;
    rpaf_server_cfg *cfg = (rpaf_server_cfg *)ap_get_module_config(r->server->module_config,
                                                                   &rpaf_module);

    if (!cfg->enable)
        return DECLINED;

    /* mod_rewrite がこの関数を再呼び出しする際に subprocess_env の HTTPS が失われる問題の回避 */
    const char *rpaf_https = apr_table_get(r->connection->notes, "rpaf_https");
    if (rpaf_https) {
        apr_table_set(r->subprocess_env, "HTTPS", rpaf_https);
        return DECLINED;
    }

    if (is_in_array(r->RPAF_IP, cfg->proxy_ips) == 1) {
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
            const char *last_val;
            rpaf_cleanup_rec *rcr;
            apr_array_header_t *arr = apr_array_make(r->pool, 0, sizeof(char*));
            /* Split the forwarded-for list, keeping only entries that look
               like an IP address and skipping (with a warning) anything else. */
            while (*fwdvalue && (val = ap_get_token(r->pool, &fwdvalue, 1))) {
                char *ip = val;
                apr_size_t len;
                while (apr_isspace(*ip))
                    ++ip;
                len = strlen(ip);
                while (len > 0 && apr_isspace(ip[len - 1]))
                    ip[--len] = '\0';
                if (rpaf_looks_like_ip(ip))
                    *(char **)apr_array_push(arr) = apr_pstrdup(r->pool, ip);
                else
                    ap_log_rerror(APLOG_MARK, APLOG_NOERRNO|APLOG_WARNING, 0, r,
                                  "mod_rpaf: X-Forwarded-For entry '%s' is not a valid IP, skipping", ip);
                if (*fwdvalue != '\0')
                    ++fwdvalue;
            }

            /* Pick the last forwarded address that is not one of our trusted
               proxies; that is the real client behind a chain of proxies. If
               there is nothing usable, leave the request untouched. */
            last_val = last_not_in_array(arr, cfg->proxy_ips);
            if (last_val == NULL)
                return DECLINED;

            rcr = (rpaf_cleanup_rec *)apr_pcalloc(r->pool, sizeof(rpaf_cleanup_rec));
            rcr->old_ip = apr_pstrdup(r->RPAF_POOL, r->RPAF_IP);
            rcr->old_family = r->RPAF_ADDR->sa.sin.sin_family;
            rcr->r = r;
            apr_pool_cleanup_register(r->pool, (void *)rcr, rpaf_cleanup, apr_pool_cleanup_null);
            r->RPAF_IP = apr_pstrdup(r->RPAF_POOL, last_val);
            r->RPAF_ADDR->sa.sin.sin_addr.s_addr = apr_inet_addr(r->RPAF_IP);
            r->RPAF_ADDR->family = AF_INET;
            r->RPAF_ADDR->sa.sin.sin_family = AF_INET;

            if (cfg->sethostname) {
                const char *hostvalue;
                /* X-Forwarded-Host (2.0 frontends) or X-Host (1.3 frontends) */
                if ((hostvalue = apr_table_get(r->headers_in, "X-Forwarded-Host")) ||
                    (hostvalue = apr_table_get(r->headers_in, "X-Host"))) {
                    /* The header may carry a comma-separated list; the last
                       entry is the one added by the nearest proxy. */
                    char *host, *last = NULL;
                    while (*hostvalue && (host = ap_get_token(r->pool, &hostvalue, 1))) {
                        apr_size_t len;
                        while (apr_isspace(*host))
                            ++host;
                        len = strlen(host);
                        while (len > 0 && apr_isspace(host[len - 1]))
                            host[--len] = '\0';
                        if (*host)
                            last = host;
                        if (*hostvalue != '\0')
                            ++hostvalue;
                    }
                    if (last) {
                        apr_table_set(r->headers_in, "Host", apr_pstrdup(r->pool, last));
                        r->hostname = apr_pstrdup(r->pool, last);
                        ap_update_vhost_from_headers(r);
                    }
                }
            }
            
            if (cfg->sethttps) {
                const char *httpsvalue;
                int is_https;
                if ((httpsvalue = apr_table_get(r->headers_in, "X-Forwarded-HTTPS")) ||
                    (httpsvalue = apr_table_get(r->headers_in, "X-HTTPS"))) {
                    is_https = rpaf_https_is_on(httpsvalue);
                } else {
                    httpsvalue = apr_table_get(r->headers_in, "X-Forwarded-Proto");
                    is_https = (httpsvalue != NULL && strcasecmp(httpsvalue, "https") == 0);
                }

                if (is_https) {
                    apr_table_set(r->connection->notes, "rpaf_https", "on");
                    apr_table_set(r->subprocess_env, "HTTPS", "on");
                    r->server->server_scheme = cfg->https_scheme;
                } else {
                    /* Reset the scheme so an earlier https request on this
                       shared server_rec does not leak into later plain
                       requests on the same vhost. */
                    r->server->server_scheme = cfg->orig_scheme;
                }
            }
            
            if (cfg->setport) {
                const char *portvalue;
                if ((portvalue = apr_table_get(r->headers_in, "X-Forwarded-Port")) ||
                    (portvalue = apr_table_get(r->headers_in, "X-Port"))) {
                    /* Set the port for this request only. Mutating
                       r->server->port corrupts the shared server_rec and
                       leaks the port into other requests and vhosts. */
                    r->parsed_uri.port     = atoi(portvalue);
                    r->parsed_uri.port_str = apr_pstrcat(r->pool, ":", portvalue, NULL);
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

static int ssl_is_https(conn_rec *c) {
    return apr_table_get(c->notes, "rpaf_https") != NULL;
}

static void register_hooks(apr_pool_t *p) {
    ap_hook_post_read_request(change_remote_ip, NULL, NULL, APR_HOOK_FIRST);

    /* mod_ssl が未ロードの場合のみ ssl_is_https を登録し、%{HTTPS} を mod_rewrite で使えるようにする */
    if (APR_RETRIEVE_OPTIONAL_FN(ssl_is_https) == NULL)
        APR_REGISTER_OPTIONAL_FN(ssl_is_https);
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

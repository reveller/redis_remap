/*
  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

  http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
*/

#include <ts/ts.h>
#include <ts/remap.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <hiredis/hiredis.h>
#include <arpa/nameser.h>

#define MAX_GET_LEN 4        // 'GET ' - simple get key
#define MAX_SCHEME_LEN 8     // 'https://'
#define MAX_PORT_LEN 6       // ':65535'
#define MAX_GET_REGEX_LEN 19 // 'GET [a-z0-9-]*[.]?/' - get key with the addition of the regex added in

// global settings
static const char *PLUGIN_NAME = "redis_remap";

// redis related global variables
redisContext *r_ctx;


bool
parse_redis_string(redisReply *redis_reply, char *scheme, char *host, int *port)
{
  bool ret_val = false;

  TSDebug(PLUGIN_NAME, "redis replied with [%s] and length [%d]\n", redis_reply->str, redis_reply->len);
  TSDebug(PLUGIN_NAME, "got the response from server : %s\n", redis_reply->str);
  TSDebug(PLUGIN_NAME, "scanf result : %d\n", sscanf(redis_reply->str, "%[a-zA-Z]://%[^:]:%d", scheme, host, port));
  TSDebug(PLUGIN_NAME, "scanf results: scheme [%s]\n", scheme);
  TSDebug(PLUGIN_NAME, "scanf results: host [%s]\n", host);
  TSDebug(PLUGIN_NAME, "scanf results: port [%d]\n", *port);
  if (sscanf(redis_reply->str, "%[a-zA-Z]://%[^:]:%d", scheme, host, port) >= 2) {
    TSDebug(PLUGIN_NAME, "\nOUTGOING REQUEST ->\n ::: to_scheme_desc: %s\n ::: to_hostname: %s\n ::: to_port: %d", scheme, host,
            *port);
    ret_val = true;
  } else {
    ret_val = false;
  }
  return ret_val;
}

bool
do_redis_remap(TSCont contp, TSHttpTxn txnp)
{
  TSMBuffer reqp;
  TSMLoc hdr_loc, url_loc, field_loc;
  bool ret_val = false;

  const char *request_host;
  int request_host_length = 0;
  const char *request_scheme;
  int request_scheme_length = 0;
  int request_port = 80;
  char get_key[MAX_GET_LEN + MAX_SCHEME_LEN + MAXDNAME + MAX_PORT_LEN + 1];
  redisReply *redis_reply;
  char oscheme[MAX_SCHEME_LEN], ohost[MAXDNAME + 1];
  int oport = 0;

  if (TSHttpTxnClientReqGet((TSHttpTxn)txnp, &reqp, &hdr_loc) != TS_SUCCESS) {
    TSDebug(PLUGIN_NAME, "could not get request data");
    return false;
  }

  if (TSHttpHdrUrlGet(reqp, hdr_loc, &url_loc) != TS_SUCCESS) {
    TSDebug(PLUGIN_NAME, "couldn't retrieve request url");
    goto release_hdr;
  }


  field_loc = TSMimeHdrFieldFind(reqp, hdr_loc, TS_MIME_FIELD_HOST, TS_MIME_LEN_HOST);

  if (!field_loc) {
    TSDebug(PLUGIN_NAME, "couldn't retrieve request HOST header");
    goto release_url;
  }

  request_host = TSMimeHdrFieldValueStringGet(reqp, hdr_loc, field_loc, -1, &request_host_length);
  if (request_host == NULL || strlen(request_host) < 1) {
    TSDebug(PLUGIN_NAME, "couldn't find request HOST header");
    goto release_field;
  }

  request_scheme = TSUrlSchemeGet(reqp, url_loc, &request_scheme_length);
  request_port = TSUrlPortGet(reqp, url_loc);

  TSDebug(PLUGIN_NAME, "      +++++REDIS REMAP+++++      ");

  TSDebug(PLUGIN_NAME, "\nINCOMING REQUEST ->\n ::: from_scheme_desc: %.*s\n ::: from_hostname: %.*s\n ::: from_port: %d",
          request_scheme_length, request_scheme, request_host_length, request_host, request_port);

  snprintf(get_key, MAX_GET_LEN + MAX_SCHEME_LEN + MAXDNAME + MAX_PORT_LEN + 1, "GET %.*s://%.*s:%d/", request_scheme_length,
           request_scheme, request_host_length, request_host, request_port);

  TSDebug(PLUGIN_NAME, "querying redis with [%s]\n", get_key);
  redis_reply = (redisReply *)redisCommand(r_ctx, get_key);

  if ((redis_reply != NULL) && (redis_reply->type == REDIS_REPLY_STRING)) {
    //  This is the normal key lookup for a domain defined as a key, ie
    //  domain.com or www.domain.com - 99.9% of the lookups
    ret_val = parse_redis_string(redis_reply, oscheme, ohost, &oport);
    TSMimeHdrFieldValueStringSet(reqp, hdr_loc, field_loc, 0, ohost, -1);
    TSUrlHostSet(reqp, url_loc, ohost, -1);
    TSUrlSchemeSet(reqp, url_loc, oscheme, -1);
    TSUrlPortSet(reqp, url_loc, oport);
  } else if ((redis_reply != NULL) && (redis_reply->type == REDIS_REPLY_NIL)) {
    //  Didn't get a value from the normal lookup, try a wildcard lookup
    TSDebug(PLUGIN_NAME, "[%s] failed to return a result from redis - checking for a wildcard that matches\n", get_key);
    char get_wildcard[MAX_GET_REGEX_LEN + MAX_SCHEME_LEN + MAXDNAME + MAX_PORT_LEN + 1];
    char temp_dom_str[MAXDNAME + 1];
    char *domain_ptr;
    int domain_len = 0;
    // request_host is not NULL termimated so I have to get it into something
    // local that I can NULL terminate and treat like a normal string
    snprintf(temp_dom_str, MAXDNAME, "%.*s", request_host_length, request_host);
    domain_ptr = strchr(temp_dom_str, '.') + 1;
    domain_len = strlen(domain_ptr);
    if (domain_ptr == NULL) {
      // Didn't find any '.' in the remainder of the domain and there
      // wasn't a match from above. So this would be for something like an
      // alias SAN, ie gdc - not found
      TSDebug(PLUGIN_NAME, "Failed to parse the subdomain from [%.*s]", request_host_length, request_host);
      goto not_found;
    } else if (domain_ptr && (strchr(domain_ptr, '.') == NULL)) {
      //  Found a '.' in the domain but there isn't another one, so this is for
      //  *.domain.com
      snprintf(get_wildcard, MAX_GET_REGEX_LEN + MAX_SCHEME_LEN + MAXDNAME + MAX_PORT_LEN + 1, "GET %.*s://[a-z0-9-]*[.]?%.*s:%d/",
               request_scheme_length, request_scheme, request_host_length, request_host, request_port);
    } else {
      //  Found a '.' and there is another one remaining, so this must be a
      //  'subdomain.domain.com'.  Ignore the subdomain and search for
      //  *.domain.com
      snprintf(get_wildcard, MAX_GET_REGEX_LEN + MAX_SCHEME_LEN + MAXDNAME + MAX_PORT_LEN + 1, "GET %.*s://[a-z0-9-]*[.]?%.*s:%d/",
               request_scheme_length, request_scheme, domain_len, domain_ptr, request_port);
    }

    TSDebug(PLUGIN_NAME, "querying redis with [%s]\n", get_wildcard);
    redis_reply = (redisReply *)redisCommand(r_ctx, get_wildcard);

    if ((redis_reply != NULL) && (redis_reply->type == REDIS_REPLY_STRING)) {
      ret_val = parse_redis_string(redis_reply, oscheme, ohost, &oport);
      TSMimeHdrFieldValueStringSet(reqp, hdr_loc, field_loc, 0, ohost, -1);
      TSUrlHostSet(reqp, url_loc, ohost, -1);
      TSUrlSchemeSet(reqp, url_loc, oscheme, -1);
      TSUrlPortSet(reqp, url_loc, oport);
      goto not_found;
    }
    TSDebug(PLUGIN_NAME, "didn't get a value returned from redis, returned [%d - %s]\n", redis_reply->type, redis_reply->str);
    goto not_found;
  } else {
    //  redis didn't returna STRING or a NIL (key not found) so it was
    //  something else
    TSDebug(PLUGIN_NAME, "didn't get any response from redis, returned [%d - %s]\n", redis_reply->type, redis_reply->str);
    goto not_found;
  }

  ret_val = true; // be sure to skip the not_found 404 return

not_found:
  // lets build up a nice 404 message for someone
  if (!ret_val) {
    TSDebug(PLUGIN_NAME, "Setting HTTP Transaction Return Status to Not Found\n");
    TSHttpTxnSetHttpRetStatus(txnp, TS_HTTP_STATUS_NOT_FOUND);
  }
  if (redis_reply) {
    TSDebug(PLUGIN_NAME, "Freeing redis reply object\n");
    freeReplyObject(redis_reply);
  }
#if (TS_VERSION_NUMBER < 2001005)
  if (request_host) {
    TSDebug(PLUGIN_NAME, "Releasing request_host string\n");
    TSHandleStringRelease(reqp, hdr_loc, request_host);
  }
  if (request_scheme) {
    TSDebug(PLUGIN_NAME, "Releasing request_schema string\n");
    TSHandleStringRelease(reqp, hdr_loc, request_scheme);
  }
#endif
release_field:
  if (field_loc) {
    TSDebug(PLUGIN_NAME, "Releasing request HOST hdr string\n");
    TSHandleMLocRelease(reqp, hdr_loc, field_loc);
  }
release_url:
  if (url_loc) {
    TSDebug(PLUGIN_NAME, "Releasing request URL string\n");
    TSHandleMLocRelease(reqp, hdr_loc, url_loc);
  }
release_hdr:
  if (hdr_loc) {
    TSDebug(PLUGIN_NAME, "Releasing request data hdr string\n");
    TSHandleMLocRelease(reqp, TS_NULL_MLOC, hdr_loc);
  }
  return ret_val;
}

static int
redis_remap(TSCont contp, TSEvent event, void *edata)
{
  TSHttpTxn txnp = (TSHttpTxn)edata;
  TSEvent reenable = TS_EVENT_HTTP_CONTINUE;

  if (event == TS_EVENT_HTTP_READ_REQUEST_HDR) {
    TSDebug(PLUGIN_NAME, "Reading Request");
    TSSkipRemappingSet(txnp, 1);
    if (!do_redis_remap(contp, txnp)) {
      reenable = TS_EVENT_HTTP_ERROR;
    }
  }

  TSHttpTxnReenable(txnp, reenable);
  return 1;
}

void
TSPluginInit(int argc, const char *argv[])
{
  TSPluginRegistrationInfo info;

  info.plugin_name = const_cast<char *>(PLUGIN_NAME);
  info.vendor_name = const_cast<char *>("Apache Software Foundation");
  info.support_email = const_cast<char *>("dev@trafficserver.apache.org");

  TSDebug(PLUGIN_NAME, "about to init redis\n");
  if (TS_SUCCESS != TSPluginRegister(TS_SDK_VERSION_2_0, &info)) {
    TSError("[%s]: plugin registration failed.\n", PLUGIN_NAME);
    return;
  }

#define MAX_HOST_LEN 128

  const char *default_host = "localhost";
  char redis_host[MAX_HOST_LEN];
  int redis_port = 6379;
  snprintf(redis_host, MAX_HOST_LEN, "%s", default_host);

  r_ctx = redisConnect(redis_host, redis_port);
  if (r_ctx->err) {
    TSError("[%s]: plugin registration failed while connecting to redis server.\n", PLUGIN_NAME);
    return;
  } else {
    TSDebug(PLUGIN_NAME, "redis connection successfully initialized");
  }

  TSCont cont = TSContCreate(redis_remap, TSMutexCreate());

  TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, cont);

  TSDebug(PLUGIN_NAME, "plugin is successfully initialized [plugin mode]");
  return;
}

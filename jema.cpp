/*
  Copyright (C) 2012 JustSystems Corporation

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */
#include <groonga/tokenizer.h>
#include <jema.h>

#include <string>
#include <vector>
#include <cstring>

#include "jmat_mark.h"

#ifndef JMAT_PROPFILE
#define JMAT_PROPFILE "/usr/local/etc/jmat.prop"
#endif

extern "C" {
GRN_API grn_obj *grn_proc_get_var_by_offset(grn_ctx *ctx,
                                            grn_user_data *user_data,
                                            unsigned int offset);
}

namespace {

grn_plugin_mutex *jema_mutex = 0;
JMAT::JEMA::DictionarySet *jema_dic = 0;
JMAT::JEMA::MorphAnalyzer *jema_ma = 0;

void jema_init(grn_ctx *ctx);
void jema_fin(grn_ctx *ctx);


void jema_init(grn_ctx *ctx)
{
  if ((jema_mutex != 0) || (jema_dic != 0) || (jema_ma != 0)) {
    GRN_PLUGIN_ERROR(ctx, GRN_TOKENIZER_ERROR,
                     "[tokenizer][jema] TokenJEMA is already initialized");
    return;
  }
  
  jema_mutex = grn_plugin_mutex_create(ctx);
  if (jema_mutex == 0) {
    jema_fin(ctx);
    GRN_PLUGIN_ERROR(ctx, GRN_NO_MEMORY_AVAILABLE,
                     "[tokenizer][jema] grn_plugin_mutex_create() failed");
    return;
  }

  int err;
  jema_dic = JMAT::JEMA::DictionarySet::openWithPropertyFile(
    JMAT_PROPFILE, err);
  if (err != JMAT::JEMA::JMAT_NO_ERROR) {
    jema_fin(ctx);
    GRN_PLUGIN_ERROR(ctx, GRN_TOKENIZER_ERROR,
                     "[tokenizer][jema] %s", JMAT::JEMA::getLastErrorMsg());
    return;
  }

  jema_ma = jema_dic->createMorphAnalyzer(err);
  if (err != JMAT::JEMA::JMAT_NO_ERROR) {
    jema_fin(ctx);
    GRN_PLUGIN_ERROR(ctx, GRN_TOKENIZER_ERROR,
                     "[tokenizer][jema] %s", JMAT::JEMA::getLastErrorMsg());
    return;
  }
}

void jema_fin(grn_ctx *ctx) {
  delete jema_ma;
  delete jema_dic;
  jema_ma = 0;
  jema_dic = 0;

  if (jema_mutex != 0) {
    grn_plugin_mutex_destroy(ctx, jema_mutex);
    jema_mutex = 0;
  }
}


struct grn_tokenizer_jema {
  grn_tokenizer_query *query;
  std::vector<std::string> tokens;
  std::size_t id;
  grn_tokenizer_token token;

  grn_tokenizer_jema() : query(0), tokens(), id(0), token() {}
  ~grn_tokenizer_jema() {}
};

void grn_tokenizer_jema_init(grn_ctx *ctx, grn_tokenizer_jema *tokenizer)
{
  new (tokenizer) grn_tokenizer_jema;
  grn_tokenizer_token_init(ctx, &tokenizer->token);
}

void grn_tokenizer_jema_fin(grn_ctx *ctx, grn_tokenizer_jema *tokenizer)
{
  grn_tokenizer_token_fin(ctx, &tokenizer->token);
  if (tokenizer->query) {
    grn_tokenizer_query_destroy(ctx, tokenizer->query);
  }
  tokenizer->~grn_tokenizer_jema();
}


grn_obj *grn_jema_init(grn_ctx *ctx, int num_args, grn_obj **args,
                       grn_user_data *user_data)
{
  grn_tokenizer_query * const query =
    grn_tokenizer_query_create(ctx, num_args, args);
  if (!query) {
    return 0;
  }

  grn_tokenizer_jema * const tokenizer = static_cast<grn_tokenizer_jema*>(
    GRN_PLUGIN_MALLOC(ctx, sizeof(grn_tokenizer_jema)));
  if (!tokenizer) {
    grn_tokenizer_query_destroy(ctx, query);
    GRN_PLUGIN_ERROR(ctx, GRN_NO_MEMORY_AVAILABLE,
                     "[tokenizer][jema] memory allocation to grn_tokenizer_jema failed");
    return 0;
  }
  grn_tokenizer_jema_init(ctx, tokenizer);
  tokenizer->query = query;

  if (query->length >= 3 && query->ptr &&
      std::memcmp(query->ptr, JMAT_QUERY_EXPANSION_MARK, 3) == 0) {
    int len = query->length - 3;
    const char* q = query->ptr + 3;
    if (len < 3 || std::memcmp(q, JMAT_NORMALIZED_MARK, 3) != 0) {
      tokenizer->tokens.push_back("");
    } else {
      len -= 3;
      q += 3;
      while (len > 0) {
        const char* p = std::strstr(q, GRN_TOKENIZER_TOKENIZED_DELIMITER_UTF8);
        if (p == NULL) {
          tokenizer->tokens.push_back(std::string(q, len));
          break;
        } else {
          tokenizer->tokens.push_back(std::string(q, p - q));
          len -= (p - q) + 3;
          q = p + 3;
        }
      }
    }
  } else {
    //GRN_LOG(ctx, GRN_LOG_DEBUG, "TokenJEMA: segment(%s)", query->ptr);
    grn_plugin_mutex_lock(ctx, jema_mutex);
    int n = jema_ma->segment(query->ptr, query->length, tokenizer->tokens);
    grn_plugin_mutex_unlock(ctx, jema_mutex);
    //GRN_LOG(ctx, GRN_LOG_DEBUG, "TokenJEMA: num of segments(%d)", n);
    if (n < 0) {
      GRN_PLUGIN_ERROR(ctx, GRN_TOKENIZER_ERROR,
                       "[tokenizer][jema] %s", jema_ma->getLastErrorMsg());
      return 0;
    }
  }

  user_data->ptr = tokenizer;

  return 0;
}

grn_obj *grn_jema_next(grn_ctx *ctx, int num_args, grn_obj **args,
                       grn_user_data *user_data)
{
  grn_tokenizer_jema * const tokenizer =
    static_cast<grn_tokenizer_jema*>(user_data->ptr);
  const grn_tokenizer_status status =
    ((tokenizer->id + 1) < tokenizer->tokens.size()) ?
    GRN_TOKENIZER_CONTINUE : GRN_TOKENIZER_LAST;
  if (tokenizer->id < tokenizer->tokens.size()) {
    const std::string &token = tokenizer->tokens[tokenizer->id++];
    grn_tokenizer_token_push(ctx, &tokenizer->token,
                             token.data(), token.size(), status);
  } else {
    grn_tokenizer_token_push(ctx, &tokenizer->token, "", 0, status);
  }
  return 0;
}
 
grn_obj *grn_jema_fin(grn_ctx *ctx, int num_args, grn_obj **args,
                      grn_user_data *user_data)
{
  grn_tokenizer_jema * const tokenizer =
      static_cast<grn_tokenizer_jema*>(user_data->ptr);
  if (tokenizer) {
    grn_tokenizer_jema_fin(ctx, tokenizer);
    GRN_PLUGIN_FREE(ctx, tokenizer);
  }
  return 0;
}
 
grn_obj *func_jema(grn_ctx *ctx, int nargs, grn_obj **args,
                   grn_user_data *user_data)
{
  grn_obj *var = grn_proc_get_var_by_offset(ctx, user_data, 0);
  const char *text = GRN_TEXT_VALUE(var);
  std::size_t len = GRN_TEXT_LEN(var);

  std::vector<std::string> tokens;
  grn_plugin_mutex_lock(ctx, jema_mutex);
  int n = jema_ma->segment(text, len, tokens);
  grn_plugin_mutex_unlock(ctx, jema_mutex);

  if (n < 0) {
    GRN_PLUGIN_ERROR(ctx, GRN_TOKENIZER_ERROR,
                     "[tokenizer][jema] %s", jema_ma->getLastErrorMsg());
    return 0;
  }

  grn_ctx_output_array_open(ctx, "RESULT", n);
  for (std::vector<std::string>::const_iterator i = tokens.begin();
       i != tokens.end(); ++i) {
    grn_ctx_output_cstr(ctx, i->c_str());
  }
  grn_ctx_output_array_close(ctx);

  return 0;
}

} // namespace


extern "C" {

/*
  GRN_PLUGIN_INIT() is called to initialize this plugin. Note that an error
  code must be set in `ctx->rc' on failure.
 */
grn_rc GRN_PLUGIN_INIT(grn_ctx *ctx)
{
  jema_init(ctx);
  return ctx->rc;
}

/*
  GRN_PLUGIN_REGISTER() registers this plugin to the database associated with
  `ctx'. The registration requires the plugin name and the functions to be
  called for tokenization.
 */
grn_rc GRN_PLUGIN_REGISTER(grn_ctx *ctx)
{
  grn_tokenizer_register(ctx, "TokenJEMA", 9,
                         grn_jema_init, grn_jema_next, grn_jema_fin);


  grn_expr_var vars[1];
  vars[0].name = "text";
  vars[0].name_size = 4;
  GRN_TEXT_INIT(&(vars[0].value), 0);
  grn_proc_create(ctx, "jema", 4, GRN_PROC_COMMAND,
                  func_jema, 0, 0,
                  1, vars);

  return ctx->rc;
}

/*
  GRN_PLUGIN_FIN() is called to finalize the plugin that was initialized by
  GRN_PLUGIN_INIT().
 */
grn_rc GRN_PLUGIN_FIN(grn_ctx *ctx)
{
  jema_fin(ctx);
  return GRN_SUCCESS;
}

}  // extern "C"

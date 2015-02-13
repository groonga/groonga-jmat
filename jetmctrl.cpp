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
#include <algorithm>
#include <cstring>
#include <groonga/plugin.h>
#include <jetm_controller.h>

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

std::vector<std::string> tmp_query;
std::vector<int> normalized;

void makeQuery(int i, std::vector<std::string>& terms,
               JMAT::JETMController *jetmctrl) 
{
  if (i == 0) {
    if (jetmctrl->getChunkSetCount() == 0) {
      return ;
    }
  }
  if (i == jetmctrl->getChunkSetCount()) {
    bool is_normalized = true;
    for (int j = 0; j < normalized.size(); j++) {
      if (!normalized[j]) {
        is_normalized = false;
        break;
      }
    }
    std::string buf;
    for (int j = 0; j < tmp_query.size(); j++) {
      if (is_normalized) {
        buf += JMAT_NORMALIZED_MARK;
      }
      buf += tmp_query[j];
    }
    terms.push_back(buf);
  } else {
    for (int j = 0; j < jetmctrl->getNthChunkCount(i); j++) {
      std::string morph_tmp;
      for (int k = 0; k < jetmctrl->getNthChunkMorphCount(i, j); k++) {
        if (k > 0) {
          morph_tmp += GRN_TOKENIZER_TOKENIZED_DELIMITER_UTF8;
        }
        morph_tmp += jetmctrl->getNthChunkMorph(i, j, k);
      }
      tmp_query.push_back(morph_tmp);
      normalized.push_back(jetmctrl->isNormalizedNthChunkTerm(i, j));
      makeQuery(i + 1, terms, jetmctrl);
      tmp_query.pop_back();
      normalized.pop_back();
    }
  }
}


grn_plugin_mutex *jetmctrl_mutex = 0;
JMAT::JMATDictionarySet *dics = 0;
JMAT::JETMController *jetmctrl = 0;

void jetmctrl_init(grn_ctx *ctx);
void jetmctrl_fin(grn_ctx *ctx);


void jetmctrl_init(grn_ctx *ctx)
{
  if ((jetmctrl_mutex != 0) || (jetmctrl != 0)) {
    GRN_PLUGIN_ERROR(ctx, GRN_TOKENIZER_ERROR,
                     "[plugin][query-expander][jetmctrl] JETMCtrl is already initialized");
    return;
  }
  
  jetmctrl_mutex = grn_plugin_mutex_create(ctx);
  if (jetmctrl_mutex == 0) {
    jetmctrl_fin(ctx);
    GRN_PLUGIN_ERROR(ctx, GRN_NO_MEMORY_AVAILABLE,
                     "[plugin][query-expander][jetmctrl] grn_plugin_mutex_create() failed");
    return;
  }

  int err;
  dics = JMAT::JMATDictionarySet::open(JMAT_PROPFILE, err);
  if (err != 0) {
    GRN_PLUGIN_ERROR(ctx, GRN_TOKENIZER_ERROR,
                     "[plugin][query-expander][jetmctrl] %s",
                     JMAT::getLastErrorMsg());
    jetmctrl_fin(ctx);
    return;
  }
  jetmctrl = dics->createJETMController(err);
  if (err != 0) {
    GRN_PLUGIN_ERROR(ctx, GRN_TOKENIZER_ERROR,
                     "[plugin][query-expander][jetmctrl] %s",
                     dics->getLastErrorMsg());
    jetmctrl_fin(ctx);
    return;
  }
}

void jetmctrl_fin(grn_ctx *ctx)
{
  delete jetmctrl;
  jetmctrl = 0;
  delete dics;
  dics = 0;

  if (jetmctrl_mutex != 0) {
    grn_plugin_mutex_destroy(ctx, jetmctrl_mutex);
    jetmctrl_mutex = 0;
  }
}


grn_rc build_query(grn_ctx *ctx, const char *query, std::size_t len,
                   grn_obj *dest)
{
  grn_rc rc = GRN_SUCCESS;

  if (len == 0) {
    return rc;
  }

  grn_plugin_mutex_lock(ctx, jetmctrl_mutex);
  if (jetmctrl->expand(query, len) < 0) {
    grn_plugin_mutex_unlock(ctx, jetmctrl_mutex);
    GRN_PLUGIN_ERROR(ctx, GRN_UNKNOWN_ERROR,
                     "[plugin][query-expander][jetmctrl] %s",
                     jetmctrl->getLastErrorMsg());
    return GRN_UNKNOWN_ERROR;
  }

  std::vector<std::string> terms;
  makeQuery(0, terms, jetmctrl);
  if (std::find(terms.begin(), terms.end(), jetmctrl->getInput()) == terms.end()) {
    terms.push_back(jetmctrl->getInput());
  }

  GRN_TEXT_PUTC(ctx, dest, '(');
  for (int i = 0; i < terms.size(); ++i) {
    if (i != 0) {
      GRN_TEXT_PUT(ctx, dest, " OR ", 4);
    }
    GRN_TEXT_PUTC(ctx, dest, '(');
    GRN_TEXT_PUT(ctx, dest, JMAT_QUERY_EXPANSION_MARK, 3);  
    GRN_TEXT_PUT(ctx, dest, terms[i].data(), terms[i].length());  
    GRN_TEXT_PUTC(ctx, dest, ')');
  }
  GRN_TEXT_PUTC(ctx, dest, ')');

  grn_plugin_mutex_unlock(ctx, jetmctrl_mutex);

  return rc;
}

grn_obj *func_query_expander_jetm(grn_ctx *ctx, int nargs, grn_obj **args,
                                  grn_user_data *user_data)
{
  grn_rc rc = GRN_END_OF_DATA;
  grn_id id;
  grn_obj *term, *expanded_term;
  void *value;
  grn_obj *rc_object;

  term = args[0];
  expanded_term = args[1];

  rc = build_query(ctx, GRN_TEXT_VALUE(term), GRN_TEXT_LEN(term),
                   expanded_term);

  rc_object = grn_plugin_proc_alloc(ctx, user_data, GRN_DB_INT32, 0);
  if (rc_object) {
    GRN_INT32_SET(ctx, rc_object, rc);
  }

  return rc_object;
}

grn_obj *func_jetm(grn_ctx *ctx, int nargs, grn_obj **args,
                   grn_user_data *user_data)
{

  grn_obj output;
  GRN_TEXT_INIT(&output, 0);

  grn_obj *var = grn_proc_get_var_by_offset(ctx, user_data, 0);
  const char *query = GRN_TEXT_VALUE(var);
  std::size_t len = GRN_TEXT_LEN(var);

  grn_rc rc = build_query(ctx, query, len, &output);
  if (rc != GRN_SUCCESS) {
    return 0;
  }

  GRN_TEXT_PUTC(ctx, &output, '\0');

  grn_ctx_output_cstr(ctx, GRN_TEXT_VALUE(&output));
  GRN_OBJ_FIN(ctx, &output);

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
  jetmctrl_init(ctx);
  return ctx->rc;
}

/*
  GRN_PLUGIN_REGISTER() registers this plugin to the database associated with
  `ctx'. The registration requires the plugin name and the functions to be
  called for tokenization.
 */
grn_rc GRN_PLUGIN_REGISTER(grn_ctx *ctx)
{
  grn_proc_create(ctx, "QueryExpanderJETM", strlen("QueryExpanderJETM"),
                  GRN_PROC_FUNCTION,
                  func_query_expander_jetm, 0, 0,
                  0, 0);

  grn_expr_var vars[1];
  vars[0].name = "query";
  vars[0].name_size = 5;
  GRN_TEXT_INIT(&(vars[0].value), 0);
  grn_proc_create(ctx, "jetm", 4, GRN_PROC_COMMAND,
                  func_jetm, 0, 0,
                  1, vars);

  return ctx->rc;
}

/*
  GRN_PLUGIN_FIN() is called to finalize the plugin that was initialized by
  GRN_PLUGIN_INIT().
 */
grn_rc GRN_PLUGIN_FIN(grn_ctx *ctx)
{
  jetmctrl_fin(ctx);
  return GRN_SUCCESS;
}

}  // extern "C"

/* SPDX-License-Identifier: Apache-2.0 */
/** @file
 * @brief YAML configuration file processing facility
 *
 * Implementation.
 *
 * Copyright (C) 2018-2022 OKTET Labs Ltd. All rights reserved.
 */

#include "conf_defs.h"
#include "conf_dh.h"
#include "conf_ta.h"
#include "conf_yaml.h"
#include "te_str.h"
#include "logic_expr.h"
#include "te_alloc.h"
#include "te_expand.h"
#include "te_file.h"

#include <ctype.h>
#include <libxml/xinclude.h>
#include <libgen.h>
#include <yaml.h>

#define CS_YAML_ERR_PREFIX "YAML configuration file parser "

#define YAML_NODE_LINE_COLUMN_FMT   "line %lu column %lu"
#define YAML_NODE_LINE_COLUMN(_n)   \
    (_n)->start_mark.line + 1, (_n)->start_mark.column + 1

#define YAML_TARGET_CONTEXT_INIT \
    { .oid = NULL, \
      .value = NULL, \
      .access = NULL, \
      .type = NULL, \
      .xmlvolatile = NULL, \
      .substitution = NULL, \
      .unit = NULL, \
      .deps = SLIST_HEAD_INITIALIZER(deps), \
      .cond = TRUE }

typedef enum cs_yaml_node_type_e {
    CS_YAML_NODE_TYPE_COMMENT,
    CS_YAML_NODE_TYPE_INCLUDE,
    CS_YAML_NODE_TYPE_COND,
    CS_YAML_NODE_TYPE_REGISTER,
    CS_YAML_NODE_TYPE_UNREGISTER,
    CS_YAML_NODE_TYPE_ADD,
    CS_YAML_NODE_TYPE_GET,
    CS_YAML_NODE_TYPE_DELETE,
    CS_YAML_NODE_TYPE_COPY,
    CS_YAML_NODE_TYPE_SET,
    CS_YAML_NODE_TYPE_REBOOT,/* Should it be added? */
} cs_yaml_node_type_t;

const te_enum_map cs_yaml_node_type_mapping[] = {
    {.name = "comment",    .value = CS_YAML_NODE_TYPE_COMMENT},
    {.name = "include",    .value = CS_YAML_NODE_TYPE_INCLUDE},
    {.name = "cond",       .value = CS_YAML_NODE_TYPE_COND},
    {.name = "register",   .value = CS_YAML_NODE_TYPE_REGISTER},
    {.name = "unregister", .value = CS_YAML_NODE_TYPE_UNREGISTER},
    {.name = "add",        .value = CS_YAML_NODE_TYPE_ADD},
    {.name = "get",        .value = CS_YAML_NODE_TYPE_GET},
    {.name = "delete",     .value = CS_YAML_NODE_TYPE_DELETE},
    {.name = "copy",       .value = CS_YAML_NODE_TYPE_COPY},
    {.name = "set",        .value = CS_YAML_NODE_TYPE_SET},
    {.name = "reboot_ta",  .value = CS_YAML_NODE_TYPE_REBOOT},
    TE_ENUM_MAP_END
};

typedef enum cs_yaml_instance_field {
    CS_YAML_INSTANCE_IF_COND,
    CS_YAML_INSTANCE_OID,
    CS_YAML_INSTANCE_VALUE,
} cs_yaml_instance_field;

const te_enum_map cs_yaml_instance_fields_mapping[] = {
    {.name = "if",      .value = CS_YAML_INSTANCE_IF_COND},
    {.name = "oid",     .value = CS_YAML_INSTANCE_OID},
    {.name = "value",   .value = CS_YAML_INSTANCE_VALUE},
    TE_ENUM_MAP_END
};

typedef enum cs_yaml_object_field {
    CS_YAML_OBJECT_D,
    CS_YAML_OBJECT_OID,
    CS_YAML_OBJECT_ACCESS,
    CS_YAML_OBJECT_TYPE,
    CS_YAML_OBJECT_UNIT,
    CS_YAML_OBJECT_DEF_VAL,
    CS_YAML_OBJECT_VOLAT,
    CS_YAML_OBJECT_SUBSTITUTION,
    CS_YAML_OBJECT_NO_PARENT_DEP,
    CS_YAML_OBJECT_DEPENDS,
} cs_yaml_object_field;

const te_enum_map cs_yaml_object_fields_mapping[] = {
    {.name = "d",             .value = CS_YAML_OBJECT_D},
    {.name = "oid",           .value = CS_YAML_OBJECT_OID},
    {.name = "access",        .value = CS_YAML_OBJECT_ACCESS},
    {.name = "type",          .value = CS_YAML_OBJECT_TYPE},
    {.name = "unit",          .value = CS_YAML_OBJECT_UNIT},
    {.name = "default",       .value = CS_YAML_OBJECT_DEF_VAL},
    {.name = "volatile",      .value = CS_YAML_OBJECT_VOLAT},
    {.name = "substitution",  .value = CS_YAML_OBJECT_SUBSTITUTION},
    {.name = "parent_dep",    .value = CS_YAML_OBJECT_NO_PARENT_DEP},
    {.name = "depends",       .value = CS_YAML_OBJECT_DEPENDS},
    TE_ENUM_MAP_END
};

const te_enum_map cs_yaml_object_access_fields_mapping[] = {
    {.name = "read_write",    .value = CFG_READ_WRITE},
    {.name = "read_only",     .value = CFG_READ_ONLY},
    {.name = "read_create",   .value = CFG_READ_CREATE},
    TE_ENUM_MAP_END
};

const te_enum_map cs_yaml_bool_mapping[] = {
    {.name = "false",     .value = FALSE},
    {.name = "true",      .value = TRUE},
    TE_ENUM_MAP_END
};

const te_enum_map cs_yaml_object_no_parent_dep_mapping[] = {
    {.name = "yes",     .value = FALSE},
    {.name = "no",      .value = TRUE},
    TE_ENUM_MAP_END
};

typedef enum cs_yaml_object_depends_field {
    CS_YAML_OBJECT_DEPENDS_OID,
    CS_YAML_OBJECT_DEPENDS_SCOPE,
} cs_yaml_object_depends_field;

const te_enum_map cs_yaml_object_depends_fields_mapping[] = {
    {.name = "oid",    .value = CS_YAML_OBJECT_DEPENDS_OID},
    {.name = "scope",  .value = CS_YAML_OBJECT_DEPENDS_SCOPE},
    TE_ENUM_MAP_END
};

const te_enum_map cs_yaml_object_depends_scope_mapping[] = {
    {.name = "object",        .value = TRUE},
    {.name = "instance",      .value = FALSE},
    TE_ENUM_MAP_END
};

#if 1
typedef struct parse_config_yaml_ctx {
    char            *file_path;
    yaml_document_t *doc;
    xmlNodePtr       xn_history;
    te_kvpair_h     *expand_vars;
    const char      *conf_dirs;
} parse_config_yaml_ctx;
#endif
#if 1
typedef struct NEW_parse_config_yaml_ctx {
    char            *file_path;
    yaml_document_t *doc;
    history_seq *history;
} NEW_parse_config_yaml_ctx;
#endif

typedef struct config_yaml_target_s {
    const char *command_name;
    const char *target_name;
} config_yaml_target_t;

static const config_yaml_target_t config_yaml_targets[] = {
    { "add", "instance" },
    { "get", "instance" },
    { "set", "instance" },
    { "delete", "instance" },
    { "copy", "instance" },
    { "register", "object" },
    { "unregister", "object" },
    { NULL, NULL}
};

static const char *
get_yaml_cmd_target(const char *cmd)
{
    const config_yaml_target_t *target = config_yaml_targets;

    for (; target->command_name != NULL; ++target)
    {
        if (strcmp(cmd, target->command_name) == 0)
            break;
    }

    return target->target_name;
}

static te_errno
get_val(const logic_expr *parsed, void *expand_vars, logic_expr_res *res)
{
    te_errno rc;
    int rc_errno;

    if (expand_vars != NULL)
        rc_errno = te_expand_kvpairs(parsed->u.value, NULL,
                                     (te_kvpair_h *)expand_vars,
                                     &res->value.simple);
    else
        rc_errno = te_expand_env_vars(parsed->u.value, NULL,
                                      &res->value.simple);
    if (rc_errno != 0)
    {
        rc = te_rc_os2te(rc_errno);
        goto out;
    }
    else
    {
        rc = 0;
    }
    res->res_type = LOGIC_EXPR_RES_SIMPLE;

out:
    return rc;
}

/**
 * Evaluate logical expression.
 *
 * @param str               String representation of the expression
 * @param res               Location for the result
 * @param expand_vars       List of key-value pairs for expansion in file,
 *                          @c NULL if environment variables are used for
 *                          substitutions
 *
 * @return Status code.
 */
static te_errno
parse_logic_expr_str(const char *str, te_bool *res, te_kvpair_h *expand_vars)
{
    logic_expr *parsed = NULL;
    logic_expr_res parsed_res;
    te_errno rc;

    rc = logic_expr_parse(str, &parsed);
    if (rc != 0)
    {
        ERROR("Failed to parse expression '%s'", str);
        goto out;
    }

    rc = logic_expr_eval(parsed, get_val, expand_vars, &parsed_res);
    if (rc != 0)
    {
        ERROR("Failed to evaluate expression '%s'", str);
        goto out;
    }

    if (parsed_res.res_type != LOGIC_EXPR_RES_BOOLEAN)
    {
        rc = TE_EINVAL;
        logic_expr_free_res(&parsed_res);
        goto out;
    }

    *res = parsed_res.value.boolean;

out:
    logic_expr_free(parsed);

    return rc;
}

#if 1
static te_errno
parse_config_if_expr(yaml_node_t *n, te_bool *if_expr, te_kvpair_h *expand_vars)
{
    const char *str = NULL;
    te_errno    rc = 0;

    if (n->type == YAML_SCALAR_NODE)
    {
        str = (const char *)n->data.scalar.value;

        if (n->data.scalar.length == 0)
        {
            ERROR(CS_YAML_ERR_PREFIX "found the if-expression node to be "
                  "badly formatted");
            return TE_EINVAL;
        }

        rc = parse_logic_expr_str(str, if_expr, expand_vars);
        if (rc != 0)
        {
            ERROR(CS_YAML_ERR_PREFIX "failed to evaluate the expression "
                  "contained in the condition node");
            return rc;
        }
    }
    else
    {
        ERROR(CS_YAML_ERR_PREFIX "found the if-expression node to be "
              "badly formatted");
        return TE_EINVAL;
    }

    return rc;
}
#endif

#if 1
static te_errno
NEW_parse_config_str(yaml_node_t *n, char **str)
{
    te_errno rc = 0;

    if (n->type == YAML_SCALAR_NODE)
    {
        *str = strdup((char *)n->data.scalar.value);

        if (n->data.scalar.length == 0)
        {
            ERROR(CS_YAML_ERR_PREFIX
                  "found the scalar node to be badly formatted");
            return TE_EINVAL;
        }
    }
    else
    {
        ERROR(CS_YAML_ERR_PREFIX
              "found the expected scalar node to be not a scalar node");
        return TE_EINVAL;
    }

    return rc;
}
#endif

#if 1
static te_errno
NEW_parse_config_str_by_mapping(yaml_node_t *n, uint8_t *num,
                                const te_enum_map mapping[])
{
    te_errno rc = 0;
    char *str = NULL;
    int i;

    rc = NEW_parse_config_str(n, &str);
    if (rc == 0)
    {
        i = te_enum_map_from_str(mapping, str, -1);
        if (i == -1)
            rc = TE_EINVAL;
        else
            *num = i;
    }
    free(str);
    return rc;
}
#endif

#if 1
static te_errno
NEW_parse_config_inst(NEW_parse_config_yaml_ctx *ctx,
                      yaml_node_t *n, instance_type *inst)
{
    yaml_document_t *d = ctx->doc;
    te_errno rc = 0;

    if (n->type == YAML_MAPPING_NODE)
    {
        yaml_node_pair_t *pair = n->data.mapping.pairs.start;

        do {
            yaml_node_t *k = yaml_document_get_node(d, pair->key);
            yaml_node_t *v = yaml_document_get_node(d, pair->value);
            int inst_field_name;

            if (k->type != YAML_SCALAR_NODE || k->data.scalar.length == 0 ||
                (v->type != YAML_SCALAR_NODE))
            {
                ERROR(CS_YAML_ERR_PREFIX "found the target attribute node to be "
                      "badly formatted");
                return TE_EINVAL;
            }
            inst_field_name = te_enum_map_from_str(
                                            cs_yaml_instance_fields_mapping,
                                            (const char *)k->data.scalar.value,
                                            -1);

            switch (inst_field_name)
            {
                case CS_YAML_INSTANCE_IF_COND:
                    rc = NEW_parse_config_str(v, &inst->if_cond);
                    break;

                case CS_YAML_INSTANCE_OID:
                    rc = NEW_parse_config_str(v, &inst->oid);
                    break;

                case CS_YAML_INSTANCE_VALUE:
                    rc = NEW_parse_config_str(v, &inst->value);
                    break;

                default:
                    ERROR(CS_YAML_ERR_PREFIX "failed to recognise the command '%s'",
                          (const char *)k->data.scalar.value);
                    rc = TE_EINVAL;
            }
            if (rc != 0)
            {
                ERROR(CS_YAML_ERR_PREFIX "failed to process %s"
                      "attribute at " YAML_NODE_LINE_COLUMN_FMT "",
                      (const char *)k->data.scalar.value,
                      YAML_NODE_LINE_COLUMN(k));
                return rc;
            }
        } while (++pair < n->data.mapping.pairs.top);
    }
    else
    {
        ERROR(CS_YAML_ERR_PREFIX
              "found the instance node to be not a mapping node");
        return TE_EINVAL;
    }
    if (inst->oid == NULL)
    {
        ERROR(CS_YAML_ERR_PREFIX
              "found the oid field is absent in instance node");
        return TE_EINVAL;
    }

    return rc;
}
#endif

#if 1
static te_errno
NEW_parse_config_obj(NEW_parse_config_yaml_ctx *ctx,
                      yaml_node_t *n, object_type *obj)
{
    yaml_document_t *d = ctx->doc;
    te_errno rc = 0;

    if (n->type == YAML_MAPPING_NODE)
    {
        yaml_node_pair_t *pair = n->data.mapping.pairs.start;

        do {
            yaml_node_t *k = yaml_document_get_node(d, pair->key);
            yaml_node_t *v = yaml_document_get_node(d, pair->value);
            int obj_field_name;
            uint8_t temp;

            if (k->type != YAML_SCALAR_NODE || k->data.scalar.length == 0 ||
                (v->type != YAML_SCALAR_NODE && v->type != YAML_SEQUENCE_NODE))
            {
                ERROR(CS_YAML_ERR_PREFIX "found the target attribute node to be "
                      "badly formatted");
                return TE_EINVAL;
            }
            obj_field_name = te_enum_map_from_str(
                                            cs_yaml_object_fields_mapping,
                                            (const char *)k->data.scalar.value,
                                            -1);

            switch (obj_field_name)
            {
                case CS_YAML_OBJECT_D:
                    rc = NEW_parse_config_str(v, &obj->d);
                    break;

                case CS_YAML_OBJECT_OID:
                    rc = NEW_parse_config_str(v, &obj->oid);
                    break;

                case CS_YAML_OBJECT_ACCESS:
                    rc = NEW_parse_config_str_by_mapping(v, &obj->access,
                                        cs_yaml_object_access_fields_mapping);
                    break;

                case CS_YAML_OBJECT_TYPE:
                    rc = NEW_parse_config_str_by_mapping(v, &obj->type,
                                                         cfg_cvt_mapping);
                    if (rc ==0 && obj->type == CVT_UNSPECIFIED)
                        rc = TE_EINVAL;
                    break;

                case CS_YAML_OBJECT_UNIT:
                    rc = NEW_parse_config_str_by_mapping(v, &temp,
                                                         cs_yaml_bool_mapping);
                    obj->unit = temp;
                    break;

                case CS_YAML_OBJECT_DEF_VAL:
                    rc = NEW_parse_config_str(v, &obj->def_val);
                    break;

                case CS_YAML_OBJECT_VOLAT:
                    rc = NEW_parse_config_str_by_mapping(v, &temp,
                                                         cs_yaml_bool_mapping);
                    obj->volat = temp;
                    break;

                case CS_YAML_OBJECT_SUBSTITUTION:
                    rc = NEW_parse_config_str_by_mapping(v, &temp,
                                                         cs_yaml_bool_mapping);
                    obj->substitution = temp;
                    break;

                case CS_YAML_OBJECT_NO_PARENT_DEP:
                    rc = NEW_parse_config_str_by_mapping(v, &temp,
                                          cs_yaml_object_no_parent_dep_mapping);
                    obj->type = temp;
                    break;

                case CS_YAML_OBJECT_DEPENDS:
#if 0
                    rc = NEW_parse_config_depends(v, &obj);
#endif
                    break;

                default:
                    ERROR(CS_YAML_ERR_PREFIX "failed to recognise the command '%s'",
                          (const char *)k->data.scalar.value);
                    rc = TE_EINVAL;
            }
            if (rc != 0)
            {
                ERROR(CS_YAML_ERR_PREFIX "failed to process %s"
                      "attribute at " YAML_NODE_LINE_COLUMN_FMT "",
                      (const char *)k->data.scalar.value,
                      YAML_NODE_LINE_COLUMN(k));
                return rc;
            }
        } while (++pair < n->data.mapping.pairs.top);
    }
    else
    {
        ERROR(CS_YAML_ERR_PREFIX
              "found the instance node to be not a mapping node");
        return TE_EINVAL;
    }
    if (obj->oid == NULL)
    {
        ERROR(CS_YAML_ERR_PREFIX
              "found the oid field is absent in instance node");
        return TE_EINVAL;
    }

    return rc;
}
#endif

typedef enum cs_yaml_node_attribute_type_e {
    CS_YAML_NODE_ATTRIBUTE_CONDITION = 0,
    CS_YAML_NODE_ATTRIBUTE_OID,
    CS_YAML_NODE_ATTRIBUTE_VALUE,
    CS_YAML_NODE_ATTRIBUTE_ACCESS,
    CS_YAML_NODE_ATTRIBUTE_TYPE,
    CS_YAML_NODE_ATTRIBUTE_VOLATILE,
    CS_YAML_NODE_ATTRIBUTE_DEPENDENCE,
    CS_YAML_NODE_ATTRIBUTE_SCOPE,
    CS_YAML_NODE_ATTRIBUTE_DESCRIPTION,
    CS_YAML_NODE_ATTRIBUTE_SUBSTITUTION,
    CS_YAML_NODE_ATTRIBUTE_UNIT,
    CS_YAML_NODE_ATTRIBUTE_UNKNOWN,
} cs_yaml_node_attribute_type_t;

static struct {
    const char                    *label;
    cs_yaml_node_attribute_type_t  type;
} const cs_yaml_node_attributes[] = {
    { "if",       CS_YAML_NODE_ATTRIBUTE_CONDITION },
    { "oid",      CS_YAML_NODE_ATTRIBUTE_OID },
    { "value",    CS_YAML_NODE_ATTRIBUTE_VALUE },
    { "access",   CS_YAML_NODE_ATTRIBUTE_ACCESS },
    { "type",     CS_YAML_NODE_ATTRIBUTE_TYPE },
    { "volatile", CS_YAML_NODE_ATTRIBUTE_VOLATILE },
    { "depends",  CS_YAML_NODE_ATTRIBUTE_DEPENDENCE },
    { "scope",    CS_YAML_NODE_ATTRIBUTE_SCOPE },
    { "d",        CS_YAML_NODE_ATTRIBUTE_DESCRIPTION },
    { "substitution", CS_YAML_NODE_ATTRIBUTE_SUBSTITUTION },
    { "unit", CS_YAML_NODE_ATTRIBUTE_UNIT },
};

static cs_yaml_node_attribute_type_t
parse_config_yaml_node_get_attribute_type(yaml_node_t *k)
{
    const char   *k_label = (const char *)k->data.scalar.value;
    unsigned int  i;

    for (i = 0; i < TE_ARRAY_LEN(cs_yaml_node_attributes); ++i)
    {
        if (strcasecmp(k_label, cs_yaml_node_attributes[i].label) == 0)
            return cs_yaml_node_attributes[i].type;
    }

    return CS_YAML_NODE_ATTRIBUTE_UNKNOWN;
}

typedef struct cytc_dep_entry {
    SLIST_ENTRY(cytc_dep_entry)  links;
    const xmlChar               *scope;
    const xmlChar               *oid;
} cytc_dep_entry;

typedef SLIST_HEAD(cytc_dep_list_t, cytc_dep_entry) cytc_dep_list_t;

typedef struct cs_yaml_target_context_s {
    const xmlChar   *oid;
    const xmlChar   *value;
    const xmlChar   *access;
    const xmlChar   *type;
    const xmlChar   *xmlvolatile;
    const xmlChar   *substitution;
    const xmlChar   *unit;
    cytc_dep_list_t  deps;
    te_bool          cond;
} cs_yaml_target_context_t;

static te_errno
parse_config_yaml_cmd_add_dependency_attribute(yaml_node_t    *k,
                                               yaml_node_t    *v,
                                               cytc_dep_entry *dep_ctx)
{
    cs_yaml_node_attribute_type_t attribute_type;

    if (k->type != YAML_SCALAR_NODE || k->data.scalar.length == 0 ||
        (v->type != YAML_SCALAR_NODE && v->type != YAML_SEQUENCE_NODE))
    {
        ERROR(CS_YAML_ERR_PREFIX "found the dependce attribute node to be "
              "badly formatted");
        return TE_EINVAL;
    }

    attribute_type = parse_config_yaml_node_get_attribute_type(k);
    switch (attribute_type)
    {
        case CS_YAML_NODE_ATTRIBUTE_OID:
            if (dep_ctx->oid != NULL)
            {
                ERROR(CS_YAML_ERR_PREFIX "detected multiple OID specifiers "
                      "of the dependce node: only one can be present");
                return TE_EINVAL;
            }

            dep_ctx->oid  = (const xmlChar *)v->data.scalar.value;
            break;

        case CS_YAML_NODE_ATTRIBUTE_SCOPE:
            if (dep_ctx->scope != NULL)
            {
                ERROR(CS_YAML_ERR_PREFIX "detected multiple scope specifiers "
                      "of the dependce node: only one can be present");
                return TE_EINVAL;
            }

            dep_ctx->scope = (const xmlChar *)v->data.scalar.value;
            break;

        case CS_YAML_NODE_ATTRIBUTE_DESCRIPTION:
            /* Ignore the description */
            break;

        default:
            if (v->type == YAML_SCALAR_NODE && v->data.scalar.length == 0)
            {
                dep_ctx->oid = (const xmlChar *)k->data.scalar.value;
            }
            else
            {
                ERROR(CS_YAML_ERR_PREFIX "failed to recognise the "
                      "attribute type in the target '%s'",
                      (const char *)k->data.scalar.value);
                return TE_EINVAL;
            }
            break;
    }

    return 0;
}

/**
 * Process an entry of the given dependency node
 *
 * @param  d       YAML document handle
 * @param  n       Handle of the parent node in the given document
 * @param  dep_ctx Entry context to store parsed properties
 *
 * @return         Status code
 */
static te_errno
parse_config_yaml_dependency_entry(yaml_document_t *d,
                                   yaml_node_t     *n,
                                   cytc_dep_entry  *dep_ctx)
{
    te_errno rc;

    if (n->type == YAML_MAPPING_NODE)
    {
        yaml_node_pair_t *pair = n->data.mapping.pairs.start;

        do {
            yaml_node_t *k = yaml_document_get_node(d, pair->key);
            yaml_node_t *v = yaml_document_get_node(d, pair->value);

            rc = parse_config_yaml_cmd_add_dependency_attribute(k, v, dep_ctx);
            if (rc != 0)
            {
                ERROR(CS_YAML_ERR_PREFIX "failed to process "
                      "attribute at " YAML_NODE_LINE_COLUMN_FMT "",
                      YAML_NODE_LINE_COLUMN(k));
                return TE_EINVAL;
            }
        } while (++pair < n->data.mapping.pairs.top);
    }
    else
    {
        ERROR(CS_YAML_ERR_PREFIX "found the dependency node to be "
              "badly formatted");
        return TE_EINVAL;
    }

    return 0;
}

/**
 * Process a dependency node of the given parent node.
 *
 * @param d     YAML document handle
 * @param n     Handle of the parent node in the given document
 * @param c     Location for the result
 *
 * @return Status code.
 */
static te_errno
parse_config_yaml_dependency(yaml_document_t            *d,
                             yaml_node_t                *n,
                             cs_yaml_target_context_t   *c)
{
    cytc_dep_entry *dep_entry;
    te_errno        rc;

    if (n->type == YAML_SCALAR_NODE)
    {
        if (n->data.scalar.length == 0)
        {
            ERROR(CS_YAML_ERR_PREFIX "found the dependency node to be "
                  "badly formatted");
            return TE_EINVAL;
        }

        dep_entry = TE_ALLOC(sizeof(*dep_entry));
        if (dep_entry == NULL) {
            ERROR(CS_YAML_ERR_PREFIX "failed to allocate memory");
            return TE_ENOMEM;
        }

        dep_entry->oid = (const xmlChar *)n->data.scalar.value;

        /* Error path resides in parse_config_yaml_cmd_process_target(). */
        SLIST_INSERT_HEAD(&c->deps, dep_entry, links);
    }
    else if (n->type == YAML_SEQUENCE_NODE)
    {
        yaml_node_item_t *item = n->data.sequence.items.start;

        do {
            yaml_node_t *in = yaml_document_get_node(d, *item);

            dep_entry = TE_ALLOC(sizeof(*dep_entry));
            if (dep_entry == NULL) {
                ERROR(CS_YAML_ERR_PREFIX "failed to allocate memory");
                return TE_ENOMEM;
            }

            rc = parse_config_yaml_dependency_entry(d, in, dep_entry);
            if (rc != 0)
            {
                free(dep_entry);
                return rc;
            }

            /* Error path resides in parse_config_yaml_cmd_process_target(). */
            SLIST_INSERT_HEAD(&c->deps, dep_entry, links);
        } while (++item < n->data.sequence.items.top);
    }
    else
    {
        ERROR(CS_YAML_ERR_PREFIX "found the dependce node to be "
              "badly formatted");
        return TE_EINVAL;
    }

    return 0;
}

static te_errno
parse_config_yaml_cmd_add_target_attribute(yaml_document_t        *d,
                                       yaml_node_t                *k,
                                       yaml_node_t                *v,
                                       cs_yaml_target_context_t   *c,
                                       te_kvpair_h                *expand_vars)
{
    cs_yaml_node_attribute_type_t attribute_type;
    te_errno                      rc = 0;

    if (k->type != YAML_SCALAR_NODE || k->data.scalar.length == 0 ||
        (v->type != YAML_SCALAR_NODE && v->type != YAML_SEQUENCE_NODE))
    {
        ERROR(CS_YAML_ERR_PREFIX "found the target attribute node to be "
              "badly formatted");
        return TE_EINVAL;
    }

    attribute_type = parse_config_yaml_node_get_attribute_type(k);
    switch (attribute_type)
    {
        case CS_YAML_NODE_ATTRIBUTE_CONDITION:
            rc = parse_config_if_expr(v, &c->cond, expand_vars);
            if (rc != 0)
            {
              ERROR(CS_YAML_ERR_PREFIX "failed to process the condition "
                    "attribute node of the target");
              return rc;
            }
            break;

        case CS_YAML_NODE_ATTRIBUTE_OID:
            if (c->oid != NULL)
            {
                ERROR(CS_YAML_ERR_PREFIX "detected multiple OID specifiers "
                      "of the target: only one can be present");
                return TE_EINVAL;
            }

            c->oid = (const xmlChar *)v->data.scalar.value;
            break;

        case CS_YAML_NODE_ATTRIBUTE_VALUE:
            if (c->value != NULL)
            {
                ERROR(CS_YAML_ERR_PREFIX "detected multiple value specifiers "
                      "of the target: only one can be present");
                return TE_EINVAL;
            }

            c->value = (const xmlChar *)v->data.scalar.value;
            break;

        case CS_YAML_NODE_ATTRIBUTE_ACCESS:
            if (c->access != NULL)
            {
                ERROR(CS_YAML_ERR_PREFIX "detected multiple access specifiers "
                      "of the target: only one can be present");
                return TE_EINVAL;
            }

            c->access = (const xmlChar *)v->data.scalar.value;
            break;

        case CS_YAML_NODE_ATTRIBUTE_TYPE:
            if (c->type != NULL)
            {
                ERROR(CS_YAML_ERR_PREFIX "detected multiple type specifiers "
                      "of the target: only one can be present");
                return TE_EINVAL;
            }

            c->type = (const xmlChar *)v->data.scalar.value;
            break;

        case CS_YAML_NODE_ATTRIBUTE_DEPENDENCE:
            rc = parse_config_yaml_dependency(d, v, c);
            if (rc != 0)
            {
                ERROR(CS_YAML_ERR_PREFIX "failed to process the dependce "
                      "node of the object");
                return rc;
            }
            break;

        case CS_YAML_NODE_ATTRIBUTE_VOLATILE:
            if (c->xmlvolatile != NULL)
            {
                ERROR(CS_YAML_ERR_PREFIX "detected multiple volatile specifiers "
                      "of the target: only one can be present");
                return TE_EINVAL;
            }

            c->xmlvolatile = (const xmlChar *)v->data.scalar.value;
            break;

        case CS_YAML_NODE_ATTRIBUTE_DESCRIPTION:
            /* Ignore the description */
            break;

        case CS_YAML_NODE_ATTRIBUTE_SUBSTITUTION:
            if (c->substitution != NULL)
            {
                ERROR(CS_YAML_ERR_PREFIX "detected multiple substitution "
                      "specifiers of the target: only one can be present");
                return TE_EINVAL;
            }

            c->substitution = (const xmlChar *)v->data.scalar.value;
            break;

        case CS_YAML_NODE_ATTRIBUTE_UNIT:
            if (c->unit != NULL)
            {
                ERROR(CS_YAML_ERR_PREFIX "detected multiple unit "
                      "specifiers of the target: only one can be present");
                return TE_EINVAL;
            }

            c->unit = (const xmlChar *)v->data.scalar.value;
            break;

        default:
            if (v->type == YAML_SCALAR_NODE && v->data.scalar.length == 0)
            {
                c->oid = (const xmlChar *)k->data.scalar.value;
            }
            else
            {
                ERROR(CS_YAML_ERR_PREFIX "failed to recognise the "
                      "attribute type in the target '%s'",
                      (const char *)k->data.scalar.value);
                return TE_EINVAL;
            }
            break;
    }

    return 0;
}

static te_errno
embed_yaml_target_in_xml(xmlNodePtr xn_cmd, xmlNodePtr xn_target,
                         cs_yaml_target_context_t *c)
{
    const xmlChar  *prop_name_oid = (const xmlChar *)"oid";
    const xmlChar  *prop_name_value = (const xmlChar *)"value";
    const xmlChar  *prop_name_access = (const xmlChar *)"access";
    const xmlChar  *prop_name_type = (const xmlChar *)"type";
    const xmlChar  *prop_name_scope = (const xmlChar *)"scope";
    const xmlChar  *prop_name_volatile = (const xmlChar *)"volatile";
    const xmlChar  *prop_name_substitution = (const xmlChar *)"substitution";
    const xmlChar  *prop_name_unit = (const xmlChar *)"unit";

    xmlNodePtr      dependency_node;
    cytc_dep_entry *dep_entry;

    if (c->oid == NULL)
    {
        ERROR(CS_YAML_ERR_PREFIX "failed to find target OID specifier");
        return TE_EINVAL;
    }

    if (!c->cond)
        return 0;

    if (xmlNewProp(xn_target, prop_name_oid, c->oid) == NULL)
    {
        ERROR(CS_YAML_ERR_PREFIX "failed to set OID for the target"
              "node in XML output");
        return TE_ENOMEM;
    }

    if (c->value != NULL &&
        xmlNewProp(xn_target, prop_name_value, c->value) == NULL)
    {
        ERROR(CS_YAML_ERR_PREFIX "failed to embed the target value "
              "attribute in XML output");
        return TE_ENOMEM;
    }

    if (c->access != NULL &&
        xmlNewProp(xn_target, prop_name_access, c->access) == NULL)
    {
        ERROR(CS_YAML_ERR_PREFIX "failed to embed the target access"
              "attribute in XML output");
        return TE_ENOMEM;
    }

    if (c->type != NULL &&
        xmlNewProp(xn_target, prop_name_type, c->type) == NULL)
    {
        ERROR(CS_YAML_ERR_PREFIX "failed to embed the target type "
              "attribute in XML output");
        return TE_ENOMEM;
    }

    if (c->xmlvolatile != NULL &&
        xmlNewProp(xn_target, prop_name_volatile, c->xmlvolatile) == NULL)
    {
        ERROR(CS_YAML_ERR_PREFIX "failed to embed the target volatile "
              "attribute in XML output");
        return TE_ENOMEM;
    }

    if (c->substitution != NULL &&
        xmlNewProp(xn_target, prop_name_substitution, c->substitution) == NULL)
    {
        ERROR(CS_YAML_ERR_PREFIX "failed to embed the target substitution"
              "attribute in XML output");
        return TE_ENOMEM;
    }

    if (c->unit != NULL &&
        xmlNewProp(xn_target, prop_name_unit, c->unit) == NULL)
    {
        ERROR(CS_YAML_ERR_PREFIX "failed to embed the target unit "
              "attribute in XML output");
        return TE_ENOMEM;
    }

    SLIST_FOREACH(dep_entry, &c->deps, links)
    {
        dependency_node = xmlNewNode(NULL, BAD_CAST "depends");
        if (dependency_node == NULL)
        {
            ERROR(CS_YAML_ERR_PREFIX "failed to allocate dependency "
                  "node for XML output");
            return TE_ENOMEM;
        }

        if (xmlNewProp(dependency_node, prop_name_oid, dep_entry->oid) ==
            NULL)
        {
            ERROR(CS_YAML_ERR_PREFIX "failed to set OID for the dependency "
                  "node in XML output");
            xmlFreeNode(dependency_node);
            return TE_ENOMEM;
        }

        if (dep_entry->scope != NULL &&
            xmlNewProp(dependency_node, prop_name_scope,
                       dep_entry->scope) == NULL)
        {
            ERROR(CS_YAML_ERR_PREFIX "failed to embed the target scope "
                  "attribute in XML output");
            xmlFreeNode(dependency_node);
            return TE_ENOMEM;
        }

        if (xmlAddChild(xn_target, dependency_node) != dependency_node)
        {
            ERROR(CS_YAML_ERR_PREFIX "failed to embed dependency node in "
                  "XML output");
            xmlFreeNode(dependency_node);
            return TE_EINVAL;
        }
    }

    if (xmlAddChild(xn_cmd, xn_target) == xn_target)
    {
        return 0;
    }
    else
    {
        ERROR(CS_YAML_ERR_PREFIX "failed to embed the target in "
              "XML output");
        return TE_EINVAL;
    }
}

static te_errno
parse_config_yaml_include_doc(parse_config_yaml_ctx *ctx, yaml_node_t *n)
{
    char *file_name;
    te_errno rc = 0;
    te_errno rc_resolve_pathname = 0;
    char *resolved_file_name = NULL;

    if (n->data.scalar.length == 0)
    {
        ERROR(CS_YAML_ERR_PREFIX "found include node to be badly formatted");
        return TE_EINVAL;
    }

    file_name = (char *)n->data.scalar.value;
    rc_resolve_pathname = te_file_resolve_pathname(file_name, ctx->conf_dirs,
                                                   F_OK, ctx->file_path,
                                                   &resolved_file_name);
    if (rc_resolve_pathname == 0)
    {
        rc = parse_config_yaml(resolved_file_name, ctx->expand_vars,
                               ctx->xn_history, ctx->conf_dirs);
    }
    else
    {
        ERROR(CS_YAML_ERR_PREFIX "document %s specified in "
              "include node is not found. "
              "te_file_resolve_pathname() produce error %d",
              file_name, rc_resolve_pathname);
        rc = TE_EINVAL;
    }
    free(resolved_file_name);
    return rc;
}

/**
 * Free memory allocated for the needs of the given YAML target context
 *
 * @param c The context
 */
static void
cytc_cleanup(cs_yaml_target_context_t *c)
{
    cytc_dep_entry *dep_entry_tmp;
    cytc_dep_entry *dep_entry;

    SLIST_FOREACH_SAFE(dep_entry, &c->deps, links, dep_entry_tmp)
    {
        SLIST_REMOVE(&c->deps, dep_entry, cytc_dep_entry, links);
        free(dep_entry);
    }
}

#if 1
/**
 * Process the given target node in the given YAML document.
 *
 * @param ctx               Current doc context
 * @param n                 Handle of the target node in the given YAML document
 * @param xn_cmd            Handle of command node in the XML document being
 *                          created
 * @param cmd               String representation of command
 *
 * @return Status code.
 */
static te_errno
parse_config_yaml_cmd_process_target(parse_config_yaml_ctx *ctx, yaml_node_t *n,
                                     xmlNodePtr xn_cmd, const char *cmd)
{
    yaml_document_t            *d = ctx->doc;
    te_kvpair_h                *expand_vars = ctx->expand_vars;
    xmlNodePtr                  xn_target = NULL;
    cs_yaml_target_context_t    c = YAML_TARGET_CONTEXT_INIT;
    const char                 *target;
    te_errno                    rc = 0;

    /*
     * Case of several included files, e.g.
     * - include:
     *      - filename1
     *      ...
     *      - filenameN
     */
    if (strcmp(cmd, "include") == 0)
    {
        rc = parse_config_yaml_include_doc(ctx, n);
        goto out;
    }

    target = get_yaml_cmd_target(cmd);
    if (target == NULL)
        return TE_EINVAL;

    xn_target = xmlNewNode(NULL, BAD_CAST target);
    if (xn_target == NULL)
    {
        ERROR(CS_YAML_ERR_PREFIX "failed to allocate %s"
              "node for XML output", target);
        return TE_ENOMEM;
    }

    if (n->type == YAML_SCALAR_NODE)
    {
        if (n->data.scalar.length == 0)
        {
            ERROR(CS_YAML_ERR_PREFIX "found the %s node to be "
                  "badly formatted", target);
            rc = TE_EINVAL;
            goto out;
        }

        c.oid = (const xmlChar *)n->data.scalar.value;
    }
    else if (n->type == YAML_MAPPING_NODE)
    {
        yaml_node_pair_t *pair = n->data.mapping.pairs.start;

        do {
            yaml_node_t *k = yaml_document_get_node(d, pair->key);
            yaml_node_t *v = yaml_document_get_node(d, pair->value);

            rc = parse_config_yaml_cmd_add_target_attribute(d, k, v, &c,
                                                            expand_vars);
            if (rc != 0)
            {
                ERROR(CS_YAML_ERR_PREFIX "failed to process %s"
                      "attribute at " YAML_NODE_LINE_COLUMN_FMT "",
                      target, YAML_NODE_LINE_COLUMN(k));
                goto out;
            }
        } while (++pair < n->data.mapping.pairs.top);
    }
    else
    {
        ERROR(CS_YAML_ERR_PREFIX "found the %s node to be "
              "badly formatted", target);
        rc = TE_EINVAL;
        goto out;
    }

    rc = embed_yaml_target_in_xml(xn_cmd, xn_target, &c);
    if (rc != 0)
        goto out;

    cytc_cleanup(&c);

    return 0;

out:
    xmlFreeNode(xn_target);
    cytc_cleanup(&c);

    return rc;
}
#endif

#if 1
/**
 * Process the given target node in the given YAML document.
 *
 * @param ctx               Current doc context
 * @param n                 Handle of the target node in the given YAML document
 * @param xn_cmd            Handle of command node in the XML document being
 *                          created
 * @param cmd               String representation of command
 *
 * @return Status code.
 */
static te_errno
NEW_parse_config_yaml_cmd_process_target(NEW_parse_config_yaml_ctx *ctx,
                                      yaml_node_t *n, history_entry *h_entry,
                                      cs_yaml_node_type_t node_type,
                                      unsigned int i)
{
//    yaml_document_t            *d = ctx->doc;
//    te_kvpair_h                *expand_vars = ctx->expand_vars;
//    xmlNodePtr                  xn_target = NULL;
//    cs_yaml_target_context_t    c = YAML_TARGET_CONTEXT_INIT;
//    const char                 *target;
    te_errno                    rc = 0;

    /*
     * Case of several included files, e.g.
     * - include:
     *      - filename1
     *      ...
     *      - filenameN
     */
    if (node_type == CS_YAML_NODE_TYPE_INCLUDE)
    {
        rc = NEW_parse_config_str(n, &h_entry->incl[i]);
        goto out;
    }

#if 0
    target = get_yaml_cmd_target(cmd);
    if (target == NULL)
        return TE_EINVAL;

    xn_target = xmlNewNode(NULL, BAD_CAST target);
    if (xn_target == NULL)
    {
        ERROR(CS_YAML_ERR_PREFIX "failed to allocate %s"
              "node for XML output", target);
        return TE_ENOMEM;
    }

    if (n->type == YAML_SCALAR_NODE)
    {
        if (n->data.scalar.length == 0)
        {
            ERROR(CS_YAML_ERR_PREFIX "found the %s node to be "
                  "badly formatted", target);
            rc = TE_EINVAL;
            goto out;
        }

        c.oid = (const xmlChar *)n->data.scalar.value;
    }
    else if (n->type == YAML_MAPPING_NODE)
    {
        yaml_node_pair_t *pair = n->data.mapping.pairs.start;

        do {
            yaml_node_t *k = yaml_document_get_node(d, pair->key);
            yaml_node_t *v = yaml_document_get_node(d, pair->value);

            rc = parse_config_yaml_cmd_add_target_attribute(d, k, v, &c,
                                                            expand_vars);
            if (rc != 0)
            {
                ERROR(CS_YAML_ERR_PREFIX "failed to process %s"
                      "attribute at " YAML_NODE_LINE_COLUMN_FMT "",
                      target, YAML_NODE_LINE_COLUMN(k));
                goto out;
            }
        } while (++pair < n->data.mapping.pairs.top);
    }
    else
    {
        ERROR(CS_YAML_ERR_PREFIX "found the %s node to be "
              "badly formatted", target);
        rc = TE_EINVAL;
        goto out;
    }

    rc = embed_yaml_target_in_xml(xn_cmd, xn_target, &c);
    if (rc != 0)
        goto out;

    cytc_cleanup(&c);

    return 0;
#endif

out:
#if 0
    xmlFreeNode(xn_target);
    cytc_cleanup(&c);

#endif
    return rc;
}
#endif

#if 1
/**
 * Process the sequence of target nodes for the specified
 * command in the given YAML document.
 *
 * @param ctx               Current doc context
 * @param n                 Handle of the target sequence in the given YAML
 *                          document
 * @param xn_cmd            Handle of command node in the XML document being
 *                          created
 * @param cmd               String representation of command
 *
 * @return Status code.
 */
static te_errno
parse_config_yaml_cmd_process_targets(parse_config_yaml_ctx *ctx,
                                      yaml_node_t *n, xmlNodePtr xn_cmd,
                                      const char *cmd)
{
    yaml_document_t *d = ctx->doc;
    yaml_node_item_t *item = n->data.sequence.items.start;

    if (n->type != YAML_SEQUENCE_NODE)
    {
        ERROR(CS_YAML_ERR_PREFIX "found the %s command's list of targets "
              "to be badly formatted", cmd);
        return TE_EINVAL;
    }

    do {
        yaml_node_t *in = yaml_document_get_node(d, *item);
        te_errno     rc = 0;

        rc = parse_config_yaml_cmd_process_target(ctx, in, xn_cmd, cmd);
        if (rc != 0)
        {
            ERROR(CS_YAML_ERR_PREFIX "failed to process the target in the "
                  "%s command's list at " YAML_NODE_LINE_COLUMN_FMT "",
                  cmd, YAML_NODE_LINE_COLUMN(in));
            return rc;
        }
    } while (++item < n->data.sequence.items.top);

    return 0;
}
#endif

#if 1
/**
 * Process the sequence of target nodes for the specified
 * command in the given YAML document.
 *
 * @param ctx               Current doc context
 * @param n                 Handle of the target sequence in the given YAML
 *                          document
 * @param xn_cmd            Handle of command node in the XML document being
 *                          created
 * @param cmd               String representation of command
 *
 * @return Status code.
 */
static te_errno
NEW_parse_config_yaml_cmd_process_targets(NEW_parse_config_yaml_ctx *ctx,
                                      yaml_node_t *n, history_entry *h_entry,
                                      cs_yaml_node_type_t node_type)
{
    yaml_document_t *d = ctx->doc;
    yaml_node_item_t *item = n->data.sequence.items.start;
    unsigned int count = 0;
    unsigned int i = 0;

    if (n->type != YAML_SEQUENCE_NODE)
    {
        ERROR(CS_YAML_ERR_PREFIX "found the %s command's list of targets "
              "to be badly formatted",
              te_enum_map_from_any_value(cfg_cvt_mapping, node_type,
                                         "unknown"));
        return TE_EINVAL;
    }
    do {
        count++;
    } while (++item < n->data.sequence.items.top);
/* should I deal with with count = 0 also? */
    switch (node_type)
    {

#define CASE_INITIATE(enum_part_, struct_part_, type_) \
        case CS_YAML_NODE_TYPE_ ## enum_part_:                          \
            h_entry-> struct_part_ ## _count = count;                   \
            h_entry-> struct_part_ = TE_ALLOC(count * sizeof(type_));   \
            break

        CASE_INITIATE(INCLUDE, incl, char *);
        CASE_INITIATE(REGISTER, reg, object_type);
        CASE_INITIATE(UNREGISTER, unreg, object_type);
        CASE_INITIATE(ADD, add, instance_type);
        CASE_INITIATE(GET, get, instance_type);
        CASE_INITIATE(DELETE, delete, instance_type);
        CASE_INITIATE(COPY, copy, instance_type);
        CASE_INITIATE(SET, set, instance_type);

#undef CASE_INITIATE

        default:
           assert(0);
    }

    item = n->data.sequence.items.start;
    do {
        yaml_node_t *in = yaml_document_get_node(d, *item);
        te_errno     rc = 0;

        /*
         * Calling NEW_parse_config_str(),
         * NEW_parse_config_obj(),
         * NEW_parse_config_inst().
         */
        switch (node_type)
        {

#define CASE_PARSE(enum_part_, struct_part_, target_) \
            case CS_YAML_NODE_TYPE_ ## enum_part_:                             \
                rc = NEW_parse_config_ ## target_(ctx, in,                     \
                                                  &h_entry-> struct_part_[i]); \
                break

            case CS_YAML_NODE_TYPE_INCLUDE:
                rc = NEW_parse_config_str(in, &h_entry->incl[i]);
                break;
#if 0
            CASE_PARSE(REGISTER, reg, obj);
            CASE_PARSE(UNREGISTER, unreg, obj);
#endif
            CASE_PARSE(ADD, add, inst);
            CASE_PARSE(GET, get, inst);
            CASE_PARSE(DELETE, delete, inst);
            CASE_PARSE(COPY, copy, inst);
            CASE_PARSE(SET, set, inst);

#undef CASE_PARSE

            default:
               assert(0);
        }
        if (rc != 0)
        {
            ERROR(CS_YAML_ERR_PREFIX "failed to process the target in the "
                  "%s command's list at " YAML_NODE_LINE_COLUMN_FMT "",
                  te_enum_map_from_any_value(cfg_cvt_mapping, node_type,
                                             "unknown"),
                  YAML_NODE_LINE_COLUMN(in));
            return rc;
        }
        i++;
    } while (++item < n->data.sequence.items.top);

    return 0;
}
#endif

#if 1
static te_errno parse_config_yaml_cmd(parse_config_yaml_ctx *ctx,
                                      yaml_node_t           *parent);
#endif
#if 1
static te_errno NEW_parse_config_yaml_cmd(NEW_parse_config_yaml_ctx *ctx,
                                          history_seq *history,
                                          yaml_node_t           *parent);
#endif

#if 1
/**
 * Process dynamic history specified command in the given YAML document.
 *
 * @param ctx               Current doc context
 * @param n                 Handle of the command node in the given YAML document
 * @param cmd               String representation of command
 *
 * @return Status code.
 */
static te_errno
parse_config_yaml_specified_cmd(parse_config_yaml_ctx *ctx, yaml_node_t *n,
                                const char *cmd)
{
    yaml_document_t *d = ctx->doc;
    te_kvpair_h     *expand_vars = ctx->expand_vars;
    xmlNodePtr       xn_history = ctx->xn_history;

    xmlNodePtr  xn_cmd = NULL;
    te_errno    rc = 0;
    te_bool     cond = FALSE;

    xn_cmd = xmlNewNode(NULL, BAD_CAST cmd);
    if (xn_cmd == NULL)
    {
        ERROR(CS_YAML_ERR_PREFIX "failed to allocate %s command "
              "node for XML output", cmd);
        return TE_ENOMEM;
    }

    if (n->type == YAML_SEQUENCE_NODE)
    {
        if (strcmp(cmd, "cond") == 0)
        {
            ERROR(CS_YAML_ERR_PREFIX "found the %s command node to be "
                  "badly formatted", cmd);
            rc = TE_EINVAL;
            goto out;
        }

        rc = parse_config_yaml_cmd_process_targets(ctx, n, xn_cmd, cmd);
        if (rc != 0)
        {
            ERROR(CS_YAML_ERR_PREFIX "detected some error(s) in the %s "
                  "command's nested node at " YAML_NODE_LINE_COLUMN_FMT "",
                  cmd, YAML_NODE_LINE_COLUMN(n));
            goto out;
        }
    }
    else if (n->type == YAML_MAPPING_NODE)
    {
        if (strcmp(cmd, "cond") != 0)
        {
            ERROR(CS_YAML_ERR_PREFIX "found the %s command node to be "
                  "badly formatted", cmd);
            rc = TE_EINVAL;
            goto out;
        }

        yaml_node_pair_t *pair = n->data.mapping.pairs.start;
        do {
            yaml_node_t *k = yaml_document_get_node(d, pair->key);
            yaml_node_t *v = yaml_document_get_node(d, pair->value);
            const char  *k_label = (const char *)k->data.scalar.value;

            if (strcmp(k_label, "if") == 0)
            {
                rc = parse_config_if_expr(v, &cond, expand_vars);
            }
            else if (strcmp(k_label, "then") == 0)
            {
                if (cond)
                    rc = parse_config_yaml_cmd(ctx, v);
            }
            else if (strcmp(k_label, "else") == 0)
            {
                if (!cond)
                    rc = parse_config_yaml_cmd(ctx, v);
            }
            else
            {
                ERROR(CS_YAML_ERR_PREFIX "failed to recognise %s "
                      "command's child", cmd);
                rc = TE_EINVAL;
            }

            if (rc != 0)
            {
                ERROR(CS_YAML_ERR_PREFIX "detected some error(s) in the %s "
                      "command's nested node at " YAML_NODE_LINE_COLUMN_FMT "",
                      cmd, YAML_NODE_LINE_COLUMN(k));
                goto out;
            }
        } while (++pair < n->data.mapping.pairs.top);
    }
    /*
     * Case of single included file, e.g.
     * - include: filename
     */
    else if (n->type == YAML_SCALAR_NODE)
    {
        if (strcmp(cmd, "include") != 0)
        {
            ERROR(CS_YAML_ERR_PREFIX "found the %s command node to be "
            "badly formatted", cmd);
            rc = TE_EINVAL;
            goto out;
        }

        rc = parse_config_yaml_include_doc(ctx, n);
        if (rc != 0)
            goto out;
    }
    else
    {
        ERROR(CS_YAML_ERR_PREFIX "found the %s command node to be "
              "badly formatted", cmd);
        rc = TE_EINVAL;
        goto out;
    }

    if (xn_cmd->children != NULL)
    {
        if (xmlAddChild(xn_history, xn_cmd) == xn_cmd)
        {
            return 0;
        }
        else
        {
            ERROR(CS_YAML_ERR_PREFIX "failed to embed %s command "
                  "to XML output", cmd);
            rc = TE_EINVAL;
        }
    }

out:
    xmlFreeNode(xn_cmd);

    return rc;
}
#endif

#if 1
static te_errno
NEW_parse_config_yaml_cond(NEW_parse_config_yaml_ctx *ctx,
                                   yaml_node_t *n,
                                   history_entry *h_entry)
{
    yaml_document_t *d = ctx->doc;
    te_errno rc = 0;

    h_entry->cond = TE_ALLOC(sizeof(cond_entry));
    yaml_node_pair_t *pair = n->data.mapping.pairs.start;
    do {
        yaml_node_t *k = yaml_document_get_node(d, pair->key);
        yaml_node_t *v = yaml_document_get_node(d, pair->value);
        const char  *k_label = (const char *)k->data.scalar.value;

        if (strcmp(k_label, "if") == 0)
        {
            rc = NEW_parse_config_str(v, &h_entry->cond->if_cond);
            if (rc != 0)
            {
                ERROR(CS_YAML_ERR_PREFIX
                      "found the if node in cond node to be badly formatted");
            }
        }
        else if (strcmp(k_label, "then") == 0)
        {
            h_entry->cond->then_cond = TE_ALLOC(sizeof(history_seq));
            rc = NEW_parse_config_yaml_cmd(ctx,
                                           h_entry->cond->then_cond, v);
            if (rc != 0)
            {
                ERROR(CS_YAML_ERR_PREFIX
                      "found the then node in cond node to be badly formatted");
            }
        }
        else if (strcmp(k_label, "else") == 0)
        {
            h_entry->cond->else_cond = TE_ALLOC(sizeof(history_seq));
            rc = NEW_parse_config_yaml_cmd(ctx,
                                           h_entry->cond->else_cond, v);
            if (rc != 0)
            {
                ERROR(CS_YAML_ERR_PREFIX
                      "found the else node in cond node to be badly formatted");
            }
        }
        else
        {
            ERROR(CS_YAML_ERR_PREFIX "failed to recognise cond "
                  "command's child");
            rc = TE_EINVAL;
        }

        if (rc != 0)
        {
            ERROR(CS_YAML_ERR_PREFIX "detected some error(s) in the cond "
                  "command's nested node at " YAML_NODE_LINE_COLUMN_FMT "",
                  YAML_NODE_LINE_COLUMN(k));
            return rc;
        }
    } while (++pair < n->data.mapping.pairs.top);

    return rc;
}
#endif

#if 1
/**
 * Process dynamic history specified command in the given YAML document.
 *
 * @param ctx               Current doc context
 * @param n                 Handle of the command node in the given YAML document
 * @param cmd               String representation of command
 *
 * @return Status code.
 */
static te_errno
NEW_parse_config_yaml_specified_cmd(NEW_parse_config_yaml_ctx *ctx,
                                    yaml_node_t *n,
                                    history_entry *h_entry,
                                    cs_yaml_node_type_t node_type)
{
    te_errno    rc = 0;

    if (n->type == YAML_SEQUENCE_NODE)
    {
        if (node_type == CS_YAML_NODE_TYPE_COMMENT ||
            node_type == CS_YAML_NODE_TYPE_COND ||
            node_type == CS_YAML_NODE_TYPE_REBOOT)
        {
            rc = TE_EINVAL;
            goto out;
        }
        rc = NEW_parse_config_yaml_cmd_process_targets(ctx, n, h_entry,
                                                       node_type);
        if (rc != 0)
        {
            ERROR(CS_YAML_ERR_PREFIX "detected some error(s) in the %s "
                  "command's nested node at " YAML_NODE_LINE_COLUMN_FMT "",
                  te_enum_map_from_any_value(cfg_cvt_mapping, node_type,
                                             "unknown"),
                  YAML_NODE_LINE_COLUMN(n));
            goto out;
        }
    }
    else if (n->type == YAML_MAPPING_NODE)
    {
        if (node_type != CS_YAML_NODE_TYPE_COND)
        {
            rc = TE_EINVAL;
            goto out;
        }
        rc = NEW_parse_config_yaml_cond(ctx, n, h_entry);
    }
    else if (n->type == YAML_SCALAR_NODE)
    {
        switch (node_type)
        {
            /*
             * Case of single included file, e.g.
             * - include: filename
             */
            case CS_YAML_NODE_TYPE_INCLUDE:
                h_entry->incl_count = 1;
                h_entry->incl = TE_ALLOC(sizeof(char *));
                rc = NEW_parse_config_str(n, h_entry->incl);
                if (rc != 0)
                    goto out;
                break;

            case CS_YAML_NODE_TYPE_COMMENT:
                rc = NEW_parse_config_str(n, &h_entry->comment);
                if (rc != 0)
                    goto out;
                break;

            case CS_YAML_NODE_TYPE_REBOOT:
                rc = NEW_parse_config_str(n, &h_entry->reboot_ta);
                if (rc != 0)
                    goto out;
                break;

            default:
                rc = TE_EINVAL;
                goto out;
        }
    }
    else
    {
        rc = TE_EINVAL;
        goto out;
    }
out:
    if (rc != 0)
    {
        ERROR(CS_YAML_ERR_PREFIX "found the %s command node to be "
              "badly formatted",
              te_enum_map_from_any_value(cfg_cvt_mapping, node_type,
                                         "unknown"));
    }

    return rc;
}
#endif

#if 1
static te_errno
parse_config_root_commands(parse_config_yaml_ctx *ctx,
                           yaml_node_t           *n)
{
    yaml_document_t *d = ctx->doc;
    yaml_node_pair_t *pair = n->data.mapping.pairs.start;
    yaml_node_t *k = yaml_document_get_node(d, pair->key);
    yaml_node_t *v = yaml_document_get_node(d, pair->value);
    te_errno rc = 0;

    if ((strcmp((const char *)k->data.scalar.value, "add") == 0) ||
        (strcmp((const char *)k->data.scalar.value, "get") == 0) ||
        (strcmp((const char *)k->data.scalar.value, "set") == 0) ||
        (strcmp((const char *)k->data.scalar.value, "register") == 0) ||
        (strcmp((const char *)k->data.scalar.value, "unregister") == 0) ||
        (strcmp((const char *)k->data.scalar.value, "delete") == 0) ||
        (strcmp((const char *)k->data.scalar.value, "copy") == 0) ||
        (strcmp((const char *)k->data.scalar.value, "include") == 0) ||
        (strcmp((const char *)k->data.scalar.value, "cond") == 0))
    {
         rc = parse_config_yaml_specified_cmd(ctx, v,
                                        (const char *)k->data.scalar.value);
    }
    else if (strcmp((const char *)k->data.scalar.value, "comment") == 0)
    {
        /* Ignore comments */
    }
    else
    {
        ERROR(CS_YAML_ERR_PREFIX "failed to recognise the command '%s'",
              (const char *)k->data.scalar.value);
        rc = TE_EINVAL;
    }

    if (rc != 0)
    {
        ERROR(CS_YAML_ERR_PREFIX "detected some error(s) in "
              "the command node at " YAML_NODE_LINE_COLUMN_FMT " in file %s",
              YAML_NODE_LINE_COLUMN(k), ctx->file_path);
    }

    return rc;
}
#endif

#if 1
static te_errno
NEW_parse_config_root_commands(NEW_parse_config_yaml_ctx *ctx,
                               history_entry *h_entry,
                               yaml_node_t           *n)
{
    yaml_document_t *d = ctx->doc;
    yaml_node_pair_t *pair = n->data.mapping.pairs.start;
    yaml_node_t *k = yaml_document_get_node(d, pair->key);
    yaml_node_t *v = yaml_document_get_node(d, pair->value);
    te_errno rc = 0;
    int node_type = te_enum_map_from_str(
                                            cs_yaml_node_type_mapping,
                                            (const char *)k->data.scalar.value,
                                            -1);

    if (node_type == -1)
    {
        ERROR(CS_YAML_ERR_PREFIX "failed to recognise the command '%s'",
              (const char *)k->data.scalar.value);
        rc = TE_EINVAL;
    }
    else
    {
         rc = NEW_parse_config_yaml_specified_cmd(ctx, v, h_entry,
                                                  node_type);
    }

    if (rc != 0)
    {
        ERROR(CS_YAML_ERR_PREFIX "detected some error(s) in "
              "the command node at " YAML_NODE_LINE_COLUMN_FMT " in file %s",
              YAML_NODE_LINE_COLUMN(k), ctx->file_path);
    }

    return rc;
}
#endif

#if 1
/**
 * Explore sequence of commands of the given parent node in the given YAML
 * document to detect and process dynamic history commands.
 *
 * @param ctx               Current doc context
 * @param parent            Parent node of sequence of commands
 *
 * @return Status code.
 */
static te_errno
parse_config_yaml_cmd(parse_config_yaml_ctx *ctx,
                      yaml_node_t           *parent)
{
    yaml_document_t  *d = ctx->doc;
    yaml_node_item_t *item = NULL;
    te_errno          rc = 0;

    if (parent->type != YAML_SEQUENCE_NODE)
    {
        ERROR(CS_YAML_ERR_PREFIX "expected sequence node");
        return TE_EFMT;
    }

    item = parent->data.sequence.items.start;
    do {
        yaml_node_t *n = yaml_document_get_node(d, *item);

        if (n->type != YAML_MAPPING_NODE)
        {
            ERROR(CS_YAML_ERR_PREFIX "found the command node to be "
                  "badly formatted");
            rc = TE_EINVAL;
        }

        rc = parse_config_root_commands(ctx, n);
        if (rc != 0)
            break;
    } while (++item < parent->data.sequence.items.top);

    return rc;
}
#endif

#if 1
/**
 * Explore sequence of commands of the given parent node in the given YAML
 * document to detect and process dynamic history commands.
 *
 * @param ctx               Current doc context
 * @param parent            Parent node of sequence of commands
 *
 * @return Status code.
 */
static te_errno
NEW_parse_config_yaml_cmd(NEW_parse_config_yaml_ctx *ctx,
                          history_seq *history,
                          yaml_node_t           *parent)
{
    yaml_document_t  *d = ctx->doc;
    yaml_node_item_t *item = NULL;
    te_errno          rc = 0;

    unsigned int i=0;

    if (parent->type != YAML_SEQUENCE_NODE)
    {
        ERROR(CS_YAML_ERR_PREFIX "expected sequence node");
        return TE_EFMT;
    }

    history->entries_count = 0;
    item = parent->data.sequence.items.start;
    do {
    history->entries_count++;
    } while (++item < parent->data.sequence.items.top);

    if (history->entries_count > 0)
        history->entries = TE_ALLOC(history->entries_count *
                                  sizeof(history_entry));
    else
        history->entries = NULL;

    item = parent->data.sequence.items.start;
    for (i = 0; i < history->entries_count; i++)
    {
        yaml_node_t *n = yaml_document_get_node(d, *item);

        if (n->type != YAML_MAPPING_NODE)
        {
            ERROR(CS_YAML_ERR_PREFIX "found the command node to be "
                  "badly formatted");
            rc = TE_EINVAL;
        }
        rc = NEW_parse_config_root_commands(ctx, &history->entries[i], n);
        if (rc != 0)
            break;
        item++;
    };

    return rc;
}
#endif

#if 1
/* See description in 'conf_yaml.h' */
te_errno
parse_config_yaml(const char *filename, te_kvpair_h *expand_vars,
                  xmlNodePtr xn_history_root, const char *conf_dirs)
{
    FILE                   *f = NULL;
    yaml_parser_t           parser;
    yaml_document_t         dy;
    xmlNodePtr              xn_history = NULL;
    yaml_node_t            *root = NULL;
    te_errno                rc = 0;
    char                   *current_yaml_file_path;
    parse_config_yaml_ctx   ctx;

    f = fopen(filename, "rb");
    if (f == NULL)
    {
        ERROR(CS_YAML_ERR_PREFIX "failed to open the target file '%s'",
              filename);
        return TE_OS_RC(TE_CS, errno);
    }

    yaml_parser_initialize(&parser);
    yaml_parser_set_input_file(&parser, f);
    yaml_parser_load(&parser, &dy);
    fclose(f);

    current_yaml_file_path = strdup(filename);
    if (current_yaml_file_path == NULL)
    {
        rc = TE_ENOMEM;
        goto out;
    }

    if (xn_history_root == NULL)
    {
        xn_history = xmlNewNode(NULL, BAD_CAST "history");
        if (xn_history == NULL)
        {
            ERROR(CS_YAML_ERR_PREFIX "failed to allocate "
                  "main history node for XML output");
            rc = TE_ENOMEM;
            goto out;
        }
    }
    else
    {
        xn_history = xn_history_root;
    }

    root = yaml_document_get_root_node(&dy);
    if (root == NULL)
    {
        ERROR(CS_YAML_ERR_PREFIX "failed to get the root node in file %s'",
              filename);
        rc = TE_EINVAL;
        goto out;
    }

    if (root->type == YAML_SCALAR_NODE &&
        root->data.scalar.value[0] == '\0')
    {
        INFO(CS_YAML_ERR_PREFIX "empty file '%s'", filename);
        rc = 0;
        goto out;
    }

    memset(&ctx, 0, sizeof(ctx));
    ctx.file_path = current_yaml_file_path;
    ctx.doc = &dy;
    ctx.xn_history = xn_history;
    ctx.expand_vars = expand_vars;
    ctx.conf_dirs = conf_dirs;
    rc = parse_config_yaml_cmd(&ctx, root);
    if (rc != 0)
    {
        ERROR(CS_YAML_ERR_PREFIX
              "encountered some error(s) on file '%s' processing",
              filename);
        goto out;
    }

    if (xn_history_root == NULL && xn_history->children != NULL)
    {
        rcf_log_cfg_changes(TRUE);
        rc = parse_config_dh_sync(xn_history, expand_vars);
        rcf_log_cfg_changes(FALSE);
    }

out:
    if (xn_history_root == NULL)
        xmlFreeNode(xn_history);
    yaml_document_delete(&dy);
    yaml_parser_delete(&parser);
    free(current_yaml_file_path);

    return rc;
}
#endif

#if 1
/* See description in 'conf_yaml.h' */
te_errno
NEW_parse_config_yaml(const char *filename, te_kvpair_h *expand_vars,
                      history_seq *history_root, const char *conf_dirs)
{
    FILE                   *f = NULL;
    yaml_parser_t           parser;
    yaml_document_t         dy;
    yaml_node_t            *root = NULL;
    te_errno                rc = 0;
    char                   *current_yaml_file_path;

    NEW_parse_config_yaml_ctx ctx;
    history_seq history = {
        .entries = NULL,
        .entries_count = 0
    };

    f = fopen(filename, "rb");
    if (f == NULL)
    {
        ERROR(CS_YAML_ERR_PREFIX "failed to open the target file '%s'",
              filename);
        return TE_OS_RC(TE_CS, errno);
    }

    yaml_parser_initialize(&parser);
    yaml_parser_set_input_file(&parser, f);
    yaml_parser_load(&parser, &dy);
    fclose(f);

    current_yaml_file_path = strdup(filename);
    if (current_yaml_file_path == NULL)
    {
        rc = TE_ENOMEM;
        goto out;
    }

    if (history_root->entries != NULL)
        history = *history_root;

    root = yaml_document_get_root_node(&dy);
    if (root == NULL)
    {
        ERROR(CS_YAML_ERR_PREFIX "failed to get the root node in file %s'",
              filename);
        rc = TE_EINVAL;
        goto out;
    }

    if (root->type == YAML_SCALAR_NODE &&
        root->data.scalar.value[0] == '\0')
    {
        INFO(CS_YAML_ERR_PREFIX "empty file '%s'", filename);
        rc = 0;
        goto out;
    }

    memset(&ctx, 0, sizeof(ctx));
    ctx.file_path = current_yaml_file_path;
    ctx.doc = &dy;
    ctx.history = &history;
    rc = NEW_parse_config_yaml_cmd(&ctx, &history, root);
    if (rc != 0)
    {
        ERROR(CS_YAML_ERR_PREFIX
              "encountered some error(s) on file '%s' processing",
              filename);
        goto out;
    }

    if (history_root->entries == NULL && history.entries != NULL)
    {
        rcf_log_cfg_changes(TRUE);
#if 0
        rc = NEW_parse_config_dh_sync(history, expand_vars);
#endif
        rcf_log_cfg_changes(FALSE);
    }

out:
    yaml_document_delete(&dy);
    yaml_parser_delete(&parser);
    free(current_yaml_file_path);

    return rc;
}
#endif

/**
 * Convert YAML backup file to XML structure
 *
 * @param file         path name of the file
 * @param root         XML structure, it should be freed
 *
 * @return int status code (see te_errno.h)
 */
te_errno
yaml_parse_backup_to_xml(const char *filename, xmlNodePtr ptr_backup)
{
    FILE *f = NULL;
    yaml_parser_t parser;
    yaml_document_t dy;
    yaml_node_t *root = NULL;
    te_errno rc = 0;
    xmlNodePtr xn_target = NULL;
    yaml_node_item_t *item;
    yaml_node_pair_t *pair;
    yaml_node_t *k;

    f = fopen(filename, "rb");
    if (f == NULL)
    {
        ERROR(CS_YAML_ERR_PREFIX "failed to open the target file '%s'",
              filename);
        return TE_OS_RC(TE_CS, errno);
    }
    yaml_parser_initialize(&parser);
    yaml_parser_set_input_file(&parser, f);
    yaml_parser_load(&parser, &dy);
    fclose(f);
    root = yaml_document_get_root_node(&dy);

    if (root == NULL)
    {
        ERROR(CS_YAML_ERR_PREFIX "failed to get the root node in file %s'",
              filename);
        rc = TE_EINVAL;
        goto out;
    }

    if (root->type == YAML_SCALAR_NODE &&
        root->data.scalar.value[0] == '\0')
    {
        INFO(CS_YAML_ERR_PREFIX "empty file '%s'", filename);
        rc = TE_EINVAL;
        goto out;
    }
    if (root->type != YAML_MAPPING_NODE)
    {
        ERROR(CS_YAML_ERR_PREFIX "error. There should be a mapping in YAML."
              "However it is %d.", root->type);
        rc = TE_EINVAL;
        goto out;
    }
    pair = root->data.mapping.pairs.start;
    k = yaml_document_get_node(&dy, pair->key);

    if (k->type != YAML_SCALAR_NODE)
    {
        ERROR(CS_YAML_ERR_PREFIX "error. "
              "There should be a scalar node in YAML");
        rc = TE_EINVAL;
        goto out;
    }
    root = yaml_document_get_node(&dy, pair->value);

    if (root->type != YAML_SEQUENCE_NODE)
    {
        ERROR(CS_YAML_ERR_PREFIX "error. There should be a sequence in YAML."
              "However it is %d.", root->type);
        rc = TE_EINVAL;
        goto out;
    }

    item = root->data.sequence.items.start;

    do {
        yaml_node_t *node_item = yaml_document_get_node(&dy, *item);
        yaml_node_pair_t *pair;
        yaml_node_t *k;
        yaml_node_t *v;
        cs_yaml_target_context_t c = YAML_TARGET_CONTEXT_INIT;
        const char *target = NULL;

        /* Here expected "object:" or "instance:" */
        if (node_item->type != YAML_MAPPING_NODE)
        {
            ERROR(CS_YAML_ERR_PREFIX "error. "
                  "There should be a mapping in YAML");
            rc = TE_EINVAL;
            goto out;
        }
        if (node_item->data.mapping.pairs.top -
            node_item->data.mapping.pairs.start != 1)
        {
            ERROR(CS_YAML_ERR_PREFIX "error. There should be a only one "
                  "key. %d+1=%d", node_item->data.mapping.pairs.start,
                  node_item->data.mapping.pairs.top);
            rc = TE_EINVAL;
            goto out;
        }

        pair = node_item->data.mapping.pairs.start;
        k = yaml_document_get_node(&dy, pair->key);

        if (k->type != YAML_SCALAR_NODE)
        {
            ERROR(CS_YAML_ERR_PREFIX "error. "
                  "There should be a scalar node in YAML");
            rc = TE_EINVAL;
            goto out;
        }

        target = (char *)k->data.scalar.value;

        if (strcmp(target, "object") != 0 && strcmp(target, "instance") != 0)
        {
            ERROR(CS_YAML_ERR_PREFIX "error. "
                  "There should be only \"object\" or \"instance\" in YAML. "
                  "However it is \"%s\"", target);
            rc = TE_EINVAL;
            goto out;
        }

        xn_target = xmlNewNode(NULL, BAD_CAST target);
        if (xn_target == NULL)
        {
            ERROR(CS_YAML_ERR_PREFIX "failed to allocate %s"
                  "node for XML output", target);
            rc = TE_ENOMEM;
            goto out;
        }

        v = yaml_document_get_node(&dy, pair->value);

        if (v->type != YAML_MAPPING_NODE)
        {
            ERROR(CS_YAML_ERR_PREFIX "error. "
                  "There should be a mapping in YAML");
            rc = TE_EINVAL;
            xmlFreeNode(xn_target);
            goto out;
        }
        pair = v->data.mapping.pairs.start;

        do {
            yaml_node_t *kk = yaml_document_get_node(&dy, pair->key);
            yaml_node_t *vv = yaml_document_get_node(&dy, pair->value);

            rc = parse_config_yaml_cmd_add_target_attribute(&dy, kk, vv,
                                                            &c, NULL);
            if (rc != 0)
            {
                ERROR(CS_YAML_ERR_PREFIX "failed to process %s"
                      "attribute at " YAML_NODE_LINE_COLUMN_FMT "",
                      target, YAML_NODE_LINE_COLUMN(k));
                cytc_cleanup(&c);
                xmlFreeNode(xn_target);
                goto out;
            }
        } while (++pair < v->data.mapping.pairs.top);

        rc = embed_yaml_target_in_xml(ptr_backup, xn_target, &c);
        cytc_cleanup(&c);

        if (rc != 0)
        {
            xmlFreeNode(xn_target);
            goto out;
        }

    } while (++item < root->data.sequence.items.top);

    if (rc != 0)
    {
        ERROR(CS_YAML_ERR_PREFIX
              "encountered some error(s) on file '%s' processing",
              filename);
        xmlFreeNode(xn_target);
        goto out;
    }

out:

    yaml_document_delete(&dy);
    yaml_parser_delete(&parser);

    return rc;
}

/* SPDX-License-Identifier: Apache-2.0 */
/** @file
 * @brief YAML configuration types and schema
 *
 * API definitions.
 *
 * Copyright (C) 2018-2022 OKTET Labs Ltd. All rights reserved.
 */

#ifndef __TE_CONF_CYAML_H__
#define __TE_CONF_CYAML_H__

#include <cyaml/cyaml.h>
#include "conf_api.h"
#include "conf_types.h"
#include "logger_api.h"

/******************************************************************************
 * C data structure for storing a backup.
 *
 * This is what we want to load the YAML into.
 ******************************************************************************/

/* enumeration for object or instance. It answers if it is object */
enum scope {
    CFG_INSTANCE =0, /* It is a default value */
    CFG_OBJECT
};

/* Structure for storing depends entry */
typedef struct depends_entry {
    char *oid;
    uint8_t scope; /* optional; 0 is default value means instance */
} depends_entry;

/* Structure for storing object */
typedef struct object_type {
    char *oid;
    uint8_t access;
    uint8_t type; /* optional; 0 is default value means NONE type */
    te_bool unit; /* optional; 0 is default value */
    char *def_val; /* optional; NULL is default value */
    te_bool volat; /* optional; 0 is default value */
    te_bool substitution; /* optional; 0 is default value */
    te_bool no_parent_dep; /* optional; 0 is default value */

    depends_entry *depends;
    uint depends_count;
} object_type;

/* Structure for storing instance */
typedef struct instance_type {
    char *oid;
    char *value; /* optional */
} instance_type;

/* Structure for storing backup entry */
typedef struct backup_entry {
/* The only one of next pointers is not NULL */
    object_type *object;
    instance_type *instance;
} backup_entry;

/* Structure for storing the whole backup*/
typedef struct backup_seq {
    backup_entry *entries;
    uint entries_count;
} backup_seq;

/******************************************************************************
 * CYAML schema to tell libcyaml about both expected YAML and data structure.
 *
 * (Our CYAML schema is just a bunch of static const data.)
 ******************************************************************************/

/* Mapping from instance or object strings to enum values for schema. */
static const cyaml_strval_t scope_strings[] = {
        {"object",   CFG_OBJECT},
        {"instance", CFG_INSTANCE},
};

/* Mapping from "access" strings to enum values for schema. */
static const cyaml_strval_t access_strings[] = {
        {"read_write",  CFG_READ_WRITE },
        {"read_only",   CFG_READ_ONLY  },
        {"read_create", CFG_READ_CREATE},
};

/* Mapping from "type" strings to flag enum for schema. */
static const cyaml_strval_t type_strings[] = {
    {"none",    CVT_NONE},
    {"bool",    CVT_BOOL},
    {"int8",    CVT_INT8},
    {"uint8",   CVT_UINT8},
    {"int16",   CVT_INT16},
    {"uint16",  CVT_UINT16},
    {"int32",   CVT_INT32},
    {"integer", CVT_INT32},
    {"uint32",  CVT_UINT32},
    {"int64",   CVT_INT64},
    {"uint64",  CVT_UINT64},
    {"string",  CVT_STRING},
    {"address", CVT_ADDRESS},
};

/* Mapping from instance or object strings to enum values for schema. */
static const cyaml_strval_t no_parent_dep_strings[] = {
        {"yes", false}, /* it is a default value */
        {"no",  true},
};

/* The depends mapping's field definitions schema is an array.
 *
 * All the field entries will refer to their parent mapping's structure,
 * in this case, `depends_entry.
 */
static const cyaml_schema_field_t depends_entry_fields_schema[] = {
    /* The first field in the mapping is an oid.
     *
     * YAML key: "oid".
     * C structure member for this key: "oid".
     *
     * Its CYAML type is string pointer, and we have no minimum or maximum
     * string length constraints.
     */
    CYAML_FIELD_STRING_PTR(
                           "oid", CYAML_FLAG_POINTER,
                            depends_entry, oid, 0, CYAML_UNLIMITED),
   /* The scope field is an enum.
    *
    * YAML key: "scope".
    * C structure member for this key: "scope".
    * Array mapping strings to values: scope_strings
    *
    * Its CYAML type is ENUM, so an array of cyaml_strval_t must be
    * provided to map from string to values.
    */
    CYAML_FIELD_ENUM(
                    "scope", CYAML_FLAG_DEFAULT | CYAML_FLAG_OPTIONAL,
                    depends_entry, scope, scope_strings,
                    CYAML_ARRAY_LEN(scope_strings)),
    /* The field array must be terminated by an entry with a NULL key.
     * Here we use the CYAML_FIELD_END macro for that. */
    CYAML_FIELD_END
};

/* The value for a depends is a mapping.
 *
 * Its fields are defined in depends_entries_fields_schema.
 */
static const cyaml_schema_value_t depends_schema = {
        CYAML_VALUE_MAPPING(CYAML_FLAG_DEFAULT,
                            depends_entry, depends_entry_fields_schema),
};

/* The object mapping's field definitions schema is an array.
 *
 * All the field entries will refer to their parent mapping's structure,
 * in this case, `object_type`.
 */
static const cyaml_schema_field_t object_fields_schema[] = {

    /* The first field in the mapping is oid.
     *
     * YAML key: "oid".
     * C structure member for this key: "oid".
     *
     * Its CYAML type is string pointer, and we have no minimum or maximum
     * string length constraints.
     */
    CYAML_FIELD_STRING_PTR(
                           "oid", CYAML_FLAG_POINTER,
                           object_type, oid, 0, CYAML_UNLIMITED),

    /* The access field is an enum.
     *
     * YAML key: "access".
     * C structure member for this key: "access".
     * Array mapping strings to values: access_strings
     *
     * Its CYAML type is ENUM, so an array of cyaml_strval_t must be
     * provided to map from string to values.
     */
    CYAML_FIELD_ENUM(
                     "access", CYAML_FLAG_DEFAULT,
                     object_type, access, access_strings,
                     CYAML_ARRAY_LEN(access_strings)),

    /* The type field is an enum.
     *
     * YAML key: "type".
     * C structure member for this key: "type".
     * Array mapping strings to values: type_strings
     *
     * Its CYAML type is ENUM, so an array of cyaml_strval_t must be
     * provided to map from string to values.
     *
     */
    CYAML_FIELD_ENUM(
                     "type", CYAML_FLAG_DEFAULT | CYAML_FLAG_OPTIONAL,
                     object_type, type, type_strings,
                     CYAML_ARRAY_LEN(type_strings)),

    /* The unit field is a te_bool.
     *
     * YAML key: "unit".
     * C structure member for this key: "unit".
     *
     */
    CYAML_FIELD_BOOL(
                     "unit", CYAML_FLAG_DEFAULT | CYAML_FLAG_OPTIONAL,
                     object_type, unit),

    /* The default field in the mapping is a string.
     *
     * YAML key: "default".
     * C structure member for this key: "def_val".
     *
     * Its CYAML type is string pointer, and we have no minimum or maximum
     * string length constraints.
     */
    CYAML_FIELD_STRING_PTR(
                           "default", CYAML_FLAG_POINTER | CYAML_FLAG_OPTIONAL,
                           object_type, def_val, 0, CYAML_UNLIMITED),

    /* The volat field is a te_bool.
     *
     * YAML key: "volatile".
     * C structure member for this key: "volat".
     *
     */
    CYAML_FIELD_BOOL(
                     "volatile", CYAML_FLAG_DEFAULT | CYAML_FLAG_OPTIONAL,
                     object_type, volat),

    /* The substitution field is a te_bool.
     *
     * YAML key: "substitution".
     * C structure member for this key: "substitution".
     *
     */
    CYAML_FIELD_BOOL(
                     "substitution", CYAML_FLAG_DEFAULT | CYAML_FLAG_OPTIONAL,
                     object_type, substitution),

    /* The parent-dep field is a te_bool but we pretend that it is a enum.
     *
     * YAML key: "parent-dep".
     * C structure member for this key: "no_parent_dep".
     *
     * Array mapping strings to values: no_parent_dep_strings
     *
     * Its CYAML type is ENUM, so an array of cyaml_strval_t must be
     * provided to map from string to values.
     */
    CYAML_FIELD_ENUM(
                     "parent-dep", CYAML_FLAG_DEFAULT | CYAML_FLAG_OPTIONAL,
                     object_type, no_parent_dep, no_parent_dep_strings,
                     CYAML_ARRAY_LEN(no_parent_dep_strings)),

    /* The next field is the instances or objects that this object depends on.
     *
     * YAML key: "depends".
     * C structure member for this key: "depends".
     *
     * Its CYAML type is a sequence.
     *
     * Since it's a sequence type, the value schema for its entries must
     * be provided too.  In this case, it's depends_schema.
     *
     * Since it's not a sequence of a fixed-length, we must tell CYAML
     * where the sequence entry count is to be stored.  In this case, it
     * goes in the "depends_count" C structure member in
     * `object_type`.
     * Since this is "depends" with the "_count" postfix, we can use
     * the following macro, which assumes a postfix of "_count" in the
     * struct member name.
     */
    CYAML_FIELD_SEQUENCE(
                         "depends",
                         CYAML_FLAG_POINTER | CYAML_FLAG_OPTIONAL,
                         object_type, depends,
                         &depends_schema, 0, CYAML_UNLIMITED),

    /* The field array must be terminated by an entry with a NULL key.
     * Here we use the CYAML_FIELD_END macro for that. */
    CYAML_FIELD_END
};

/* The instance mapping's field definitions schema is an array.
 *
 * All the field entries will refer to their parent mapping's structure.
 */
static const cyaml_schema_field_t instance_fields_schema[] = {
    /* The first field in the mapping is oid.
     *
     * YAML key: "oid".
     * C structure member for this key: "oid".
     *
     * Its CYAML type is string pointer, and we have no minimum or maximum
     * string length constraints.
     */
    CYAML_FIELD_STRING_PTR(
                           "oid", CYAML_FLAG_POINTER,
                           instance_type, oid, 0, CYAML_UNLIMITED),

    /* The value field in the mapping is a string.
     *
     * YAML key: "value".
     * C structure member for this key: "value".
     *
     * Its CYAML type is string pointer, and we have no minimum or maximum
     * string length constraints.
     */
    CYAML_FIELD_STRING_PTR(
                           "value", CYAML_FLAG_POINTER | CYAML_FLAG_OPTIONAL,
                           instance_type, value, 0, CYAML_UNLIMITED),

    /* The field array must be terminated by an entry with a NULL key.
     * Here we use the CYAML_FIELD_END macro for that. */
    CYAML_FIELD_END
};

/* The backup_entry mapping's field definitions schema is an array.
 *
 * All the field entries will refer to their parent mapping's structure,
 * in this case, `backup_entry`.
 */
static const cyaml_schema_field_t backup_entry_fields_schema[] = {
    /* The first field is the object.
     *
     * YAML key: "object".
     * C structure member for this key: "object".
     *
     * Its CYAML type is a mapping.
     *
     * Since it's a mapping type, the schema for its mapping's fields must
     * be provided too. In this case, it's object_fields_schema.
     */
    CYAML_FIELD_MAPPING_PTR(
                            "object", CYAML_FLAG_POINTER | CYAML_FLAG_OPTIONAL,
                            backup_entry, object, object_fields_schema),
     /* The next field is the instance.
     *
     * YAML key: "instance".
     * C structure member for this key: "instance".
     *
     * Its CYAML type is a mapping.
     *
     * Since it's a mapping type, the schema for its mapping's fields must
     * be provided too. In this case, it's instance_fields_schema.
     */
    CYAML_FIELD_MAPPING_PTR(
                            "instance", CYAML_FLAG_POINTER | CYAML_FLAG_OPTIONAL,
                            backup_entry, instance, instance_fields_schema),
    /* The field array must be terminated by an entry with a NULL key.
     * Here we use the CYAML_FIELD_END macro for that. */
    CYAML_FIELD_END
};

/* The value for a backup_entry is a mapping.
 *
 * Its fields are defined in backup_entry_fields_schema.
 */
static const cyaml_schema_value_t backup_entry_schema = {

        CYAML_VALUE_MAPPING(CYAML_FLAG_DEFAULT,
                                        backup_entry, backup_entry_fields_schema),
};

/* The backup mapping's field definitions schema is an array.
 *
 * All the field entries will refer to their parent mapping's structure,
 * in this case, `backup_seq`.
 */
static const cyaml_schema_field_t backup_fields_schema[] = {
    /* The only field is the instances or objects.
     *
     * YAML key: "backup".
     * C structure member for this key: "entries".
     *
     * Its CYAML type is a sequence.
     *
     * Since it's a sequence type, the value schema for its entries must
     * be provided too. In this case, it's backup_entry_schema.
     *
     * Since it's not a sequence of a fixed-length, we must tell CYAML
     * where the sequence entry count is to be stored. In this case, it
     * goes in the "entries_count" C structure member in
     * `backup_seq`.
     * Since this is "entries" with the "_count" postfix, we can use
     * the following macro, which assumes a postfix of "_count" in the
     * struct member name.
     */
    CYAML_FIELD_SEQUENCE(
                         "backup",
                         CYAML_FLAG_POINTER | CYAML_FLAG_OPTIONAL,
                         backup_seq, entries,
                         &backup_entry_schema, 0, CYAML_UNLIMITED),
    /* The field array must be terminated by an entry with a NULL key.
     * Here we use the CYAML_FIELD_END macro for that. */
    CYAML_FIELD_END
};

/* Top-level schema. The top level value for the backup is a sequence.
 *
 * Its fields are defined in backup_entry_fields_schema.
 */
static const cyaml_schema_value_t backup_schema = {
    CYAML_VALUE_MAPPING(CYAML_FLAG_POINTER,
                        backup_seq, backup_fields_schema),
};


/* Our CYAML config.
 *
 * If you don't want to change it between calls, make it const.
 *
 * Here we have a very basic config that difined in conf_cyaml.c.
 */
extern cyaml_config_t cyaml_config;


/**
 * Process CYAML errors to TE errors
 *
 * @param err          CYAML error
 *
 * @return status code (see te_errno.h)
 */
extern te_errno
te_process_cyaml_errors(cyaml_err_t err);

/* That prints some part of debug. DOES IT NEEDED AT ALL?
 */
extern void print_backup(backup_seq *backup);
#endif /* __TE_CONF_CYAML_H__ */


#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "conf_cyaml.h"

/******************************************************************************
 * Actual code to load and save YAML doc using libcyaml.
 ******************************************************************************/
/* Our CYAML config.
 *
 * If you don't want to change it between calls, make it const.
 *
 * Here we have a very basic config.
 */
cyaml_config_t cyaml_config = {
    .log_fn = cyaml_log,            /* Use the default logging function. */
    .mem_fn = cyaml_mem,            /* Use the default memory allocator. */
    .log_level = CYAML_LOG_WARNING, /* Logging errors and warnings only. */
    /*  .log_level = CYAML_LOG_DEBUG,   for logging debug. */
};

te_errno
te_process_cyaml_errors(cyaml_err_t err)
{
    switch (err)
    {
        /**< Success. */
        case CYAML_OK:
            return 0;

        /* Memory allocation failed. */
        case CYAML_ERR_OOM:
            ERROR("There is a CYAML error %d", err);
            return TE_ENOMEM;

        /* See \ref CYAML_CFG_NO_ALIAS. */
        case CYAML_ERR_ALIAS:
        /* Failed to open file. */
        case CYAML_ERR_FILE_OPEN:
        /* Mapping key rejected by schema. */
        case CYAML_ERR_INVALID_KEY:
        /* Value rejected by schema. */
        case CYAML_ERR_INVALID_VALUE:
        /* No anchor found for alias. */
        case CYAML_ERR_INVALID_ALIAS:
        /* Internal error in LibCYAML. */
        case CYAML_ERR_INTERNAL_ERROR:
        /* YAML event rejected by schema. */
        case CYAML_ERR_UNEXPECTED_EVENT:
        /* String length too short. */
        case CYAML_ERR_STRING_LENGTH_MIN:
        /* String length too long. */
        case CYAML_ERR_STRING_LENGTH_MAX:
        /* Value's data size unsupported. */
        case CYAML_ERR_INVALID_DATA_SIZE:
        /* Top level type must be pointer. */
        case CYAML_ERR_TOP_LEVEL_NON_PTR:
        /* Schema contains invalid type. */
        case CYAML_ERR_BAD_TYPE_IN_SCHEMA:
        /* Schema minimum exceeds maximum. */
        case CYAML_ERR_BAD_MIN_MAX_SCHEMA:
        /* Bad seq_count param for schema. */
        case CYAML_ERR_BAD_PARAM_SEQ_COUNT:
        /* Client gave NULL data argument. */
        case CYAML_ERR_BAD_PARAM_NULL_DATA:
        /* Bit value beyond bit field size. */
        case CYAML_ERR_BAD_BITVAL_IN_SCHEMA:
        /* Too few sequence entries. */
        case CYAML_ERR_SEQUENCE_ENTRIES_MIN:
        /* Too many sequence entries. */
        case CYAML_ERR_SEQUENCE_ENTRIES_MAX:
        /* Mismatch between min and max. */
        case CYAML_ERR_SEQUENCE_FIXED_COUNT:
        /* Non-fixed sequence in sequence. */
        case CYAML_ERR_SEQUENCE_IN_SEQUENCE:
        /* Required mapping field missing. */
        case CYAML_ERR_MAPPING_FIELD_MISSING:
        /* Client gave NULL mem function. */
        case CYAML_ERR_BAD_CONFIG_NULL_MEMFN:
        /* Client gave NULL config arg. */
        case CYAML_ERR_BAD_PARAM_NULL_CONFIG:
        /* Client gave NULL schema arg. */
        case CYAML_ERR_BAD_PARAM_NULL_SCHEMA:
        /* Failed to initialise libyaml. */
        case CYAML_ERR_LIBYAML_EMITTER_INIT:
        /* Failed to initialise libyaml. */
        case CYAML_ERR_LIBYAML_PARSER_INIT:
        /* Failed to initialise libyaml. */
        case CYAML_ERR_LIBYAML_EVENT_INIT:
        /* Error inside libyaml emitter. */
        case CYAML_ERR_LIBYAML_EMITTER:
        /* Error inside libyaml parser. */
        case CYAML_ERR_LIBYAML_PARSER:
        /* Count of CYAML return codes. */
        case CYAML_ERR__COUNT:
        {
            ERROR("There is a CYAML error %s(%d)",
                  cyaml_strerror(err), err);
            return TE_EINVAL;
        }
        default:
        assert(0);
    }
}


/* That prints some part of debug.
 */
void print_backup(backup_seq *backup)
{
//    int i;

    printf("Backup entries: %u\n", backup->entries_count);
    for (unsigned int i = 0;
         i < backup->entries_count;
         i+=(backup->entries_count-i)<20?1:10)
    {
        if (backup->entries[i].object != NULL)
//            printf("%u. object. %s\n", i + 1, backup->entries[i].object->oid);
            ERROR("%u. object. %s\n", i + 1, backup->entries[i].object->oid);
        if (backup->entries[i].instance != NULL)
//            printf("%u. instance. %s\n", i + 1, backup->entries[i].instance->oid);
            ERROR("%u. instance. %s\n", i + 1, backup->entries[i].instance->oid);
    }
}
#if 0
/* Main entry point from OS. */
int main(int argc, char *argv[])
{
    cyaml_err_t err;
    backup_seq *backup;
    enum {
        ARG_PROG_NAME = 0,
        ARG_PATH_IN,
        ARG_PATH_OUT,
        ARG__COUNT,
    };
    uint8_t temp;
    unsigned i;

    /* Handle args */
    if (argc != ARG__COUNT)
    {
        fprintf(stderr, "Usage:\n");
        fprintf(stderr, "  %s <INPUT> <OUTPUT>\n", argv[ARG_PROG_NAME]);
        return EXIT_FAILURE;
    }
    /* Load input file. */
    err = cyaml_load_file(argv[ARG_PATH_IN], &config,
                          &backup_schema, (void **) &backup, NULL);
    if (err != CYAML_OK)
    {
        fprintf(stderr, "ERROR: %s\n", cyaml_strerror(err));
        return EXIT_FAILURE;
    }

    /* Use the data. */
    print_backup(backup);

    /* Modify the data */
    char *testoid;
    testoid = strdup("test");

    struct object_s *test_obj;
    test_obj = malloc(sizeof(struct object_s));
    test_obj->oid = testoid;
    test_obj->access = NULL;
    test_obj->type = NULL;
    test_obj->unit = NULL;
    test_obj->def_val = NULL;
    test_obj->depends = NULL;
    test_obj->depends_count = 0;
    //*test_obj = { testoid, NULL, NULL, NULL, NULL, NULL, 0};

    backup->entries_count++;
    backup->entries = reallocarray(backup->entries,
                                   backup->entries_count,
                                   sizeof(struct backup_entry_s));

    backup->entries[backup->entries_count-1].object = test_obj;
    backup->entries[backup->entries_count-1].instance = NULL;

    print_backup(backup);
/* Create new YAML file */
    struct backup_s backup1;
    uint8_t type;

    backup1.entries_count=2;
    backup1.entries = malloc(sizeof(struct backup_entry_s)*2);
    backup1.entries[0].instance = NULL;
    backup1.entries[0].object = malloc(sizeof(struct object_s));
    backup1.entries[0].object->oid = strdup(":::");
    backup1.entries[0].object->type = malloc(sizeof(uint8_t));
    *backup1.entries[0].object->type = CVT_STRING;
    backup1.entries[0].object->access = NULL;
    backup1.entries[0].object->unit = NULL;
    backup1.entries[0].object->def_val = NULL;
    backup1.entries[0].object->def_val = NULL;
    backup1.entries[0].object->depends = malloc(sizeof(struct depends_s)*1);
    backup1.entries[0].object->depends_count = 1;
    backup1.entries[0].object->depends[0].oid = strdup("dep_oid");
    backup1.entries[0].object->depends[0].scope = 1;

    backup1.entries[1].object = NULL;
    backup1.entries[1].instance = malloc(sizeof(struct instance_s));
    backup1.entries[1].instance->oid = strdup("test_oid_instance");
    backup1.entries[1].instance->value = strdup("value_test_oid_instance");
    printf("%d\n", *backup1.entries[0].object->type);

    err = cyaml_save_file("res1.yaml", &config,
                          &backup_schema, &backup1, 0);
    cyaml_free(&config, &depends_schema, backup1.entries[0].object->depends, 0);
    /* Save data to new YAML file. */
    err = cyaml_save_file(argv[ARG_PATH_OUT], &config,
                          &backup_schema, backup, 0);
    if (err != CYAML_OK)
    {
        fprintf(stderr, "ERROR: %s\n", cyaml_strerror(err));
        cyaml_free(&config, &backup_schema, backup, 0);
        return EXIT_FAILURE;
    }

    /* Free the data */
    cyaml_free(&config, &backup_schema, backup, 0);

    return EXIT_SUCCESS;
}
#endif

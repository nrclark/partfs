#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include <libfdisk/libfdisk.h>

#include "fdisk_access.h"

enum {
    buffer_size = 1024
};

int partition_count(const char *devname)
{
    int prev_errno = errno;
    int result = 0;
    struct fdisk_table *table = NULL;
    struct fdisk_context *ctx = NULL;
    errno = 0;

    if (devname == NULL) {
        result = FDISK_NULL_PTR;
        goto cleanup;
    }

    if ((ctx = fdisk_new_context()) == NULL) {
        result = FDISK_CONTEXT_FAIL;
        goto cleanup;
    }

    if (access(devname, R_OK) != 0) {
        result = FDISK_INVALID_FILE;
        goto cleanup;
    }

    if (fdisk_assign_device(ctx, devname, true) != 0) {
        result = FDISK_ACCESS_DEVICE;
        goto cleanup;
    }

    if (fdisk_get_partitions(ctx, &table) != 0) {
        result = FDISK_READ_PARTITIONS;
        goto cleanup;
    }

    result = fdisk_table_get_nents(table);

cleanup:
    if (table) {
        fdisk_unref_table(table);
    }
    if (ctx) {
        fdisk_unref_context(ctx);
    }

    if (errno == 0) {
        errno = prev_errno;
    } else {
        fprintf(stderr, "Error accessing %s: %s.\n", devname, strerror(errno));
    }

    return result;
}

int partition_get_info(const char *devname, unsigned int partnum,
                       struct part_info **info)
{
    int prev_errno = errno;
    int result = 0;
    struct fdisk_table *table = NULL;
    struct fdisk_context *ctx = NULL;
    struct fdisk_partition *partition = NULL;
    struct fdisk_parttype *type = NULL;
    unsigned long ssize = 0;
    const char *buffer = NULL;
    errno = 0;

    if ((devname == NULL) || (info == NULL)) {
        result = FDISK_NULL_PTR;
        goto cleanup;
    }

    if ((ctx = fdisk_new_context()) == NULL) {
        result = FDISK_CONTEXT_FAIL;
        goto cleanup;
    }

    if (access(devname, R_OK) != 0) {
        result = FDISK_INVALID_FILE;
        goto cleanup;
    }

    if (fdisk_assign_device(ctx, devname, true) != 0) {
        result = FDISK_ACCESS_DEVICE;
        goto cleanup;
    }

    if (fdisk_get_partitions(ctx, &table) != 0) {
        result = FDISK_READ_PARTITIONS;
        goto cleanup;
    }

    if (partnum >= fdisk_table_get_nents(table)) {
        result = FDISK_MISSING_PARTITION;
        goto cleanup;
    }

    if ((partition = fdisk_table_get_partition(table, partnum)) == NULL) {
        result = FDISK_CORRUPT_PARTITION;
        goto cleanup;
    }

    if (fdisk_partition_has_start(partition) == false) {
        result = FDISK_CORRUPT_PARTITION;
        goto cleanup;
    }

    if (fdisk_partition_has_size(partition) == false) {
        result = FDISK_CORRUPT_PARTITION;
        goto cleanup;
    }

    *info = calloc(1, sizeof(struct part_info));
    if (*info == NULL) {
        result = FDISK_ALLOC_FAILURE;
        goto cleanup;
    }

    ssize = fdisk_get_sector_size(ctx);
    (*info)->start = (off_t) (fdisk_partition_get_start(partition) * ssize);
    (*info)->length = (off_t) (fdisk_partition_get_size(partition) * ssize);

    if ((type = fdisk_partition_get_type(partition)) == NULL) {
        result = FDISK_CORRUPT_PARTITION;
        goto cleanup;
    }

    if (((*info)->name = calloc(1, buffer_size)) == NULL) {
        result = FDISK_ALLOC_FAILURE;
        goto cleanup;
    }

    if (((*info)->uuid = calloc(1, buffer_size)) == NULL) {
        result = FDISK_ALLOC_FAILURE;
        goto cleanup;
    }

    if (((*info)->type = calloc(1, buffer_size)) == NULL) {
        result = FDISK_ALLOC_FAILURE;
        goto cleanup;
    }

    ((*info)->name)[0] = '\x00';
    ((*info)->name)[buffer_size - 1] = '\x00';
    ((*info)->uuid)[0] = '\x00';
    ((*info)->uuid)[buffer_size - 1] = '\x00';
    ((*info)->type)[0] = '\x00';
    ((*info)->type)[buffer_size - 1] = '\x00';

    if ((buffer = fdisk_parttype_get_name(type)) != NULL) {
        strncpy((*info)->type, buffer, strnlen(buffer, buffer_size - 1));
    }

    if ((buffer = fdisk_partition_get_name(partition)) != NULL) {
        strncpy((*info)->name, buffer, strnlen(buffer, buffer_size - 1));
    }

    if ((buffer = fdisk_partition_get_uuid(partition)) != NULL) {
        strncpy((*info)->uuid, buffer, strnlen(buffer, buffer_size - 1));
    }

    result = 0;

cleanup:
    if (table) {
        fdisk_unref_table(table);
    }
    if (ctx) {
        fdisk_unref_context(ctx);
    }

    if (errno == 0) {
        errno = prev_errno;
    } else {
        fprintf(stderr, "Error accessing %s: %s.\n", devname, strerror(errno));
    }

    return result;
}

void partition_dealloc_info(struct part_info *info)
{
    if (info == NULL) {
        return;
    }

    if (info->name != NULL) {
        free(info->name);
    }

    if (info->uuid != NULL) {
        free(info->uuid);
    }

    if (info->type != NULL) {
        free(info->type);
    }

    free(info);
}

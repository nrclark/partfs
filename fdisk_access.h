#ifndef FDISK_ACCESS_H
#define FDISK_ACCESS_H

#include <sys/types.h>

enum {
    FDISK_NULL_PTR = -1,
    FDISK_CONTEXT_FAIL = -2,
    FDISK_INVALID_FILE = -3,
    FDISK_ACCESS_DEVICE = -4,
    FDISK_READ_PARTITIONS = -5,
    FDISK_MISSING_PARTITION = -6,
    FDISK_CORRUPT_PARTITION = -7,
    FDISK_ALLOC_FAILURE = -8
};

struct part_info {
    off_t start;
    off_t length;
    char *name;
    char *uuid;
    char *type;
};

int partition_count(const char *devname);

int partition_get_info(const char *devname, unsigned int partnum,
                       struct part_info **info);

void partition_dealloc_info(struct part_info *info);

#endif

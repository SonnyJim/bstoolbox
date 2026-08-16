#ifndef BSTOOLBOX_H
#define BSTOOLBOX_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <sys/stat.h>
#include <sys/types.h>

#include "os.h"

#define SCSI_INQUIRY                    0x12
#define BLUESCSI_TOOLBOX_MODE_FILES     0xD0
#define BLUESCSI_TOOLBOX_GET_FILE       0xD1
#define BLUESCSI_TOOLBOX_COUNT_FILES    0xD2
#define BLUESCSI_TOOLBOX_SEND_FILE_PREP 0xD3
#define BLUESCSI_TOOLBOX_SEND_FILE_10   0xD4
#define BLUESCSI_TOOLBOX_SEND_FILE_END  0xD5
#define BLUESCSI_TOOLBOX_TOGGLE_DEBUG   0xD6
#define BLUESCSI_TOOLBOX_MODE_CDS       0xD7
#define BLUESCSI_TOOLBOX_SET_NEXT_CD    0xD8
#define BLUESCSI_TOOLBOX_MODE_DEVICES   0xD9
#define BLUESCSI_TOOLBOX_COUNT_CDS      0xDA

#define BLUESCSI_TOOLBOX_METADATA	0xD9
#define BLUESCSI_TOOLBOX_METADATA_LIST_DEVICES	0x00
#define BLUESCSI_TOOLBOX_METADATA_GET_CAP	0x01
#define BLUESCSI_TOOLBOX_METADATA_SET_WDIR	0x02
#define BLUESCSI_TOOLBOX_METADATA_GET_WDIR	0x03


#define BLUESCSI_TOOLBOX_API_VER 1

#define MAX_FILES 100
#define NAME_BUF_SIZE 33
#define NOT_ACTIVE -1
#define SCSI_CMD_LENGTH 10

/* New File Transfer Transfer Constants */
#define GET_BLOCK_SIZE         4096
#define GET_BLOCKS_PER_XFER    8
#define GET_BUF_SIZE           (GET_BLOCK_SIZE * GET_BLOCKS_PER_XFER)

#define SEND_BLOCK_SIZE        512
#define SEND_BLOCKS_PER_XFER   127   /* Request 127 x 512B = 65,024B (~63.5KB) per SEND command */
#define SEND_BUF_SIZE          (SEND_BLOCK_SIZE * SEND_BLOCKS_PER_XFER)

typedef enum
{
	TYPE_NONE = 0xFF,
	TYPE_HDD = 0x00,
	TYPE_REMOVABLE = 0x01,
	TYPE_CD = 0x02,
	TYPE_FLOPPY = 0x03,
	TYPE_MO = 0x04,
	TYPE_SEQUENTIAL = 0x05
} dev_type;

extern int device_list[8];

enum {
	MODE_NONE, 
	MODE_CD,
	MODE_SHARED,
	MODE_PUT,
	MODE_INQUIRY,
	MODE_GET_WDIR,
	MODE_SET_WDIR,
	MODE_GET_LOG,
	MODE_DEBUG
};

enum {
	PRINT_OFF,
	PRINT_ON
};

enum {
	DEBUG_SET,
	DEBUG_GET
};

extern int verbose;

typedef struct {
	unsigned char dev_type;
	unsigned char dev_type_mod;
	unsigned char version;
	unsigned char add_length;
	char reserved[3]; 
	char vendor_id[9];
	char product_id[17];
	char product_rev[33];
} scsi_inquiry;

typedef struct {
    unsigned char index;
    unsigned char type;
    char name[NAME_BUF_SIZE];
    unsigned char size[5];
} ToolboxFileEntry;

extern ToolboxFileEntry files[MAX_FILES];
extern int files_count;

#endif

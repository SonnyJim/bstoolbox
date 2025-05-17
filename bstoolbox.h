#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <sys/stat.h>
#include <sys/types.h>

#include "os.h"

#define SCSI_INQUIRY    0x12
#define BLUESCSI_TOOLBOX_COUNT_FILES    0xD2
#define BLUESCSI_TOOLBOX_MODE_FILES     0xD0
#define BLUESCSI_TOOLBOX_GET_FILE       0xD1
#define BLUESCSI_TOOLBOX_SEND_FILE_PREP 0xD3
#define BLUESCSI_TOOLBOX_SEND_FILE_10   0xD4
#define BLUESCSI_TOOLBOX_SEND_FILE_END  0xD5
#define BLUESCSI_TOOLBOX_TOGGLE_DEBUG   0xD6
#define BLUESCSI_TOOLBOX_MODE_CDS       0xD7
#define BLUESCSI_TOOLBOX_SET_NEXT_CD    0xD8
#define BLUESCSI_TOOLBOX_MODE_DEVICES   0xD9
#define BLUESCSI_TOOLBOX_COUNT_CDS      0xDA
#define OPEN_RETRO_SCSI_TOO_MANY_FILES 0x0001

#define MAX_FILES 100
#define MAX_DATA_LEN 4096 //TODO Document
#define SEND_BUF_SIZE 512
#define NAME_BUF_SIZE 33
#define NOT_ACTIVE -1
#define SCSI_CMD_LENGTH 10 //Almost all of the SCSI commands we send are 10 big
//Copied from scsi2sd.h
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

int device_list[8];

enum {
	MODE_NONE, 
	MODE_CD,
	MODE_SHARED,
	MODE_PUT,
	MODE_INQUIRY,
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

int verbose;

typedef struct {
	unsigned char dev_type; // Peripheral device type (bits 4-7), Peripheral qualifier (bits 0-3)
	unsigned char dev_type_mod;    //RMB (bit 7), Device-type modifier (bits 0-6)
	unsigned char version; //SCSI version ID
	unsigned char add_length; //Additional length in bytes
	char reserved[3]; 
	char vendor_id[9];
	char product_id[17];
	char product_rev[33];
	
} scsi_inquiry;

typedef struct {
    unsigned char index;   /* byte 00: file index in directory */
    unsigned char type;    /* byte 01: type 0 = file, 1 = directory */
    char name[NAME_BUF_SIZE];         /* byte 02-34: filename (32 byte max) + space for NUL terminator */
    unsigned char size[5]; /* byte 35-39: file size (40 bit big endian unsigned) */
} ToolboxFileEntry;

ToolboxFileEntry files[MAX_FILES];
int files_count;
//char device_path[256];

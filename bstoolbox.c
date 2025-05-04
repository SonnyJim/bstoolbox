/*
 * BlueSCSI v2 IRIX tools
 */

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

#define MAX_FILES 100 //TODO Hmmmm
#define MAX_DATA_LEN 4096

enum {
	TYPE_NONE = 255,
	TYPE_HDD = 0x00,
	TYPE_CD = 0x02
} dev_type;

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
    char name[33];         /* byte 02-34: filename (32 byte max) + space for NUL terminator */
    unsigned char size[5]; /* byte 35-39: file size (40 bit big endian unsigned) */
} ToolboxFileEntry;

#define SEND_BUF_SIZE 512
#define NAME_BUF_SIZE 33

static int bluescsi_sendfile (int dev, char *path)
{
	char cmd[10] = {BLUESCSI_TOOLBOX_SEND_FILE_PREP, 0, 0, 0, 0, 0, 0, 0, 0, 0};	
	char filename[NAME_BUF_SIZE];
	char *base_name;
	char send_buf[SEND_BUF_SIZE]; //Send buffer is 512 bytes
	long int bytes_read = 0;
	long int bytes_left = 0;
	int buf_idx= 0; //offset in 512 byte chunks
	int ret;
	FILE *fd;
	long int filesize;
	struct stat st; //Struct to get filesize
	if (verbose)
		fprintf (stdout, "sendfile: %s\n", path);

	memset(send_buf, 0, sizeof(send_buf));
	// Extract filename from path
	base_name = strrchr(path, '/');
	if (base_name == NULL) {
		base_name = path; // No '/' found, the path is the filename
	} else {
		base_name++; // Move past '/'
	}
	    // Ensure filename fits in buffer
	if (strlen(base_name) >= NAME_BUF_SIZE) {
		fprintf(stderr, "Error: sendfile Filename too long: %s\n", base_name);
		return -1;
	}
	strncpy(filename, base_name, NAME_BUF_SIZE);
	filename[NAME_BUF_SIZE - 1] = '\0'; // Ensure null-termination

	fd = fopen(path, "rb");
	if ( fd == NULL)
	{
		fprintf (stderr, "Error: sendfile couldn't open %s\n", path);
		return 1;
	}
    	// Use stat to get file size
    	if (stat(path, &st) == 0) {
		if (verbose)
			printf("File size of %s is %lld bytes\n", filename, (long long)st.st_size);
	} else {
		fprintf (stderr, "Error: sendfile couldn't stat %s\n", path);
		fclose(fd);
        	return 1;
    	}
	filesize = st.st_size;

	//Send the name as data
	if (scsi_send_commandw(dev, (unsigned char *)cmd, sizeof(cmd), (unsigned char *)filename, MAX_DATA_LEN) != 0)
	{
		fprintf (stderr, "Error: sendfileprep failed - %s\n", strerror(errno));
			fclose(fd);
		return 1;
	}
	
	//Construct the command and start sending the file
	cmd[0] = BLUESCSI_TOOLBOX_SEND_FILE_10;
	
	while (bytes_read < filesize){
		
		if (bytes_left < SEND_BUF_SIZE)
		{
			bytes_read += fread(send_buf, 1, bytes_left, fd); 
			cmd[1] = (bytes_left & 0xFF00) >> 8;
			cmd[2] = (bytes_left & 0xFF);
		}
		else
		{
			cmd[1] = (sizeof(send_buf) & 0xFF00) >> 8;
			cmd[2] = (sizeof(send_buf) & 0xFF);
			bytes_read += fread(send_buf, 1, sizeof(send_buf), fd); 
		}

		cmd[3] = (buf_idx & 0xFF0000) >> 16;
		cmd[4] = (buf_idx & 0xFF00) >> 8;
		cmd[5] = (buf_idx & 0xFF);

		if (bytes_left < SEND_BUF_SIZE)
			ret = scsi_send_commandw(dev, (unsigned char *)cmd, sizeof(cmd), (unsigned char *)send_buf, bytes_left );
		else
			ret = scsi_send_commandw(dev, (unsigned char *)cmd, sizeof(cmd), (unsigned char *)send_buf, sizeof(send_buf));
		if (ret != 0)
		{
			fprintf (stderr, "Error: sendfile10 failed - %s\n", strerror(errno));
			fclose(fd);
			return 1;
		}
		buf_idx++;
		bytes_left = filesize - bytes_read;
	
	}
	memset (cmd, 0, sizeof(cmd));
	cmd[0] = BLUESCSI_TOOLBOX_SEND_FILE_END;

	if (scsi_send_command(dev, (unsigned char *)cmd, sizeof(cmd), (unsigned char *)NULL, 0) != 0)
	{
		fprintf (stderr, "Error: sendfileend failed - %s\n", strerror(errno));
		fclose(fd);
		return 1;
	}


	fclose(fd);
	return 0;
}

//Check and set debug status
static int bluescsi_getdebug (int dev)
{
	int ret;
	char cmd[10] = {BLUESCSI_TOOLBOX_TOGGLE_DEBUG, 0, 0, 0, 0, 0, 0, 0, 0, 0};	
	char buf[1];
	cmd[1] = DEBUG_GET;//Get debug flag
	memset(buf, 0, sizeof(buf));
	if (scsi_send_command(dev, (unsigned char *)cmd, sizeof(cmd), (unsigned char *)buf, sizeof(buf)) != 0)
	{
		fprintf (stderr, "Error: getdebug failed - %s\n", strerror(errno));
		return -1;
	}
	ret = buf[0];
	return ret;
}

static int bluescsi_setdebug (int dev, int value)
{
	char cmd[10] = {BLUESCSI_TOOLBOX_TOGGLE_DEBUG, 0, 0, 0, 0, 0, 0, 0, 0, 0};
	
	if (value > 1)
		value = 1;
	else if (value < 0)
		value = 0;
	cmd[1] = DEBUG_SET;
	cmd[2] = value;
	if (scsi_send_command(dev, (unsigned char *)cmd, sizeof(cmd), (unsigned char *)NULL, 0) != 0)
	{
		fprintf (stderr, "Error: BlueSCSI setdebug failed - %s\n", strerror(errno));
		return -1;
	}

	if (verbose)
		fprintf (stdout, "Debug mode set to: %i\n", bluescsi_getdebug (dev));
	return 0;
}


//Returns the number of files in the /shared directory
static int bluescsi_countfiles(int dev)
{
	char cmd[10] = {BLUESCSI_TOOLBOX_COUNT_FILES, 0, 0, 0, 0, 0, 0, 0, 0, 0};	
	char buf[1];
	int ret;
	memset(buf, 0, sizeof(buf));
	if (scsi_send_command(dev, (unsigned char *)cmd, sizeof(cmd), (unsigned char *)buf, sizeof(buf)) != 0)
	{
		fprintf (stderr, "Error: countfiles failed - %s\n", strerror(errno));
		return -1;
	}
	ret = buf[0]; //Maximum of 100 files 
	return ret;
}

/** TOOLBOX_COUNT_CDS (read, length 10)
 * Input:
 *  CDB 00 = command byte
 * Output:
 *  Single byte indicating number of CD images available. (Max 100.)
 */
static int bluescsi_countcds(int dev)
{
	char cmd[10] = {BLUESCSI_TOOLBOX_COUNT_CDS, 0, 0, 0, 0, 0, 0, 0, 0, 0};	
	char buf[1];
	int ret;
	memset(buf, 0, sizeof(buf));
	if (scsi_send_command(dev, (unsigned char *)cmd, sizeof(cmd), (unsigned char *)buf, sizeof(buf)) != 0)
	{
		fprintf (stderr, "Error: countcds failed - %s\n", strerror(errno));
		return -1;
	}
	ret = buf[0]; //Maximum of 100 files
	if (ret < 0 || ret > MAX_FILES)
	{
		fprintf (stderr,"Error: countcds invalid count %i\n", ret);
		return -1;
	}
	return ret;
}

/** TOOLBOX_SET_NEXT_CD (read, length 10)
 * Input:
 *  CDB 00 = command byte
 *  CDB 01 = image file index byte to change to
 * Output:
 *  None.
 */
static int bluescsi_setnextcd(int dev, int num)
{
	int max_cds;
	char cmd[10];
	memset(cmd, 0, sizeof(cmd));	
	max_cds = bluescsi_countcds(dev);
	cmd[0] = BLUESCSI_TOOLBOX_SET_NEXT_CD;

	if (num < 0 | num > max_cds)
	{
		fprintf (stderr, "setnextcd: %i is out of range of max %i\n", num, max_cds);
		return 1;
	}

	cmd[1] = num;
	if (verbose)
		fprintf (stdout, "%i set as next CD\n", cmd[1]);	
	if (scsi_send_command(dev, (unsigned char *)cmd, 10, (unsigned char *)NULL, 0) != 0)
	{
		fprintf (stderr, "Error: setnextcd failed - %s\n", strerror(errno));
		return -1;
	}
	return 0;
}

static int bluescsi_listcds(int dev)
{
	char cmd[10] = {BLUESCSI_TOOLBOX_MODE_CDS, 0, 0, 0, 0, 0, 0, 0, 0, 0};
	char *buf;
	int i, j;
	int buf_size;
	int num_cds;

	num_cds = bluescsi_countcds (dev);
	if (num_cds == 0 || num_cds > MAX_FILES)
	{
		fprintf (stderr, "Error:  CD number requested invalid, is device a CD?: %i\n", num_cds);
		return -1;
	}
	fprintf (stdout, "Found %i CDs\n", num_cds);
	buf_size = sizeof(ToolboxFileEntry);
	buf_size = buf_size * num_cds;
	
	buf = (char *)malloc(buf_size);
	memset(buf, 0, sizeof(buf));
	if (scsi_send_command(dev, (unsigned char *)cmd, sizeof(cmd), (unsigned char *)buf, buf_size) != 0)
	{
		fprintf (stderr, "Error: listcds failed - %s\n", strerror(errno));
		return -1;
	}
	j = 0;
	for (i=0;i<buf_size;i++)
	{

		if (j == 0 )
			fprintf (stdout, "#%i ", (buf[i]));
		if (j >= 2 && j <= 34)
			fprintf (stdout, "%c", buf[i]);
		j++;
		if (j >= sizeof(ToolboxFileEntry))
		{
			j = 0;
			fprintf (stdout, "\n");
		}
	//	fprintf (stdout, "%02x",buf[i]);
	}
	fprintf (stdout, "\n");
	return 1;
}

ToolboxFileEntry files[MAX_FILES];
int files_count;

static long int size_to_long(const unsigned char size[5])
{
    int i;
    long int result = 0;
    for (i = 0; i < 5; i++)
    {
        result = (result << 8) | size[i];
    }
    return result;
}

static int bluescsi_listfiles(int dev, int print)
{
	char cmd[10] = {BLUESCSI_TOOLBOX_MODE_FILES, 0, 0, 0, 0, 0, 0, 0, 0, 0};
	char *buf;
	int i;
	int buf_size;
	int num_files;
	
	if (verbose)
		fprintf (stdout, "Listing files on %i\n", dev);

	num_files = bluescsi_countfiles (dev);
	if (num_files == 0 || num_files > MAX_FILES)
	{
		fprintf (stderr, "Error: listfiles num_files invalid: %i", num_files);
		return -1;
	}
	files_count = num_files;
	if (verbose)
		fprintf (stdout, "Found %i files\n", num_files);
	buf_size = sizeof(ToolboxFileEntry);
	buf_size = buf_size * num_files;
	
	buf = (char *)malloc(buf_size);
	memset(buf, 0, sizeof(buf));
	if (scsi_send_command(dev, (unsigned char *)cmd, sizeof(cmd), (unsigned char *)buf, buf_size) != 0)
	{
		fprintf (stderr, "Error: listfiles failed - %s\n", strerror(errno));
		return -1;
	}
	//Copy SCSI data to global files var	
	for (i = 0; i < num_files; i++) {
		memcpy(&files[i], buf + i * sizeof(ToolboxFileEntry), sizeof(ToolboxFileEntry));
	}
	if (verbose || print)
	{	
		for (i = 0;i < num_files;i++)
			fprintf (stdout, "#%i %s %li bytes\n", files[i].index, files[i].name, size_to_long(files[i].size));
	}
	return 0;
}

//Get file from /shared directory, grabbed in chunks of 4096
static int bluescsi_getfile(int dev, int idx, char *outdir)
{
	char cmd[10];
	char buf[MAX_DATA_LEN];
	FILE *fd;
	char *filename;
	size_t bytes_written;
	size_t bytes_left;
	int blk_idx = 0;


	memset(cmd, 0, sizeof(cmd));
	cmd[0] = BLUESCSI_TOOLBOX_GET_FILE;
	cmd[1] = idx;
	cmd[2] = 0; //Index offset in MAX_DATA_LEN blocks

	if (strlen (outdir) < 2)//Default to current directory
		strcpy (outdir, "./");
	//We need to populate the files struct first before doing anything else
	if (bluescsi_listfiles (dev, 0) != 0)
	{
		fprintf (stderr, "Error: getfile couldn't listfiles\n");
		return -1;
	}
	
	if (verbose)
		fprintf (stdout, "getfile :#%i %s %li bytes\n", files[idx].index, files[idx].name, size_to_long(files[idx].size));
	filename = malloc (strlen(outdir) + strlen(files[idx].name));
	strcpy (filename, outdir);
	strcat (filename, files[idx].name);
	if (verbose)
		fprintf (stdout, "Writing to file %s\n", filename);
	fd = fopen(filename, "wb");
	if (fd == NULL)
	{
		fprintf (stderr, "Error: getfile couldn't open %s\n", filename);
		return -1;
	}
	memset(buf, 0, sizeof(buf));
	bytes_left = 0;
	bytes_written = 0;

	//Read the data from the SCSI bus and store to disk 
	//TODO timeouts and sanity checks
	while (1)
	{	
		if (scsi_send_command(dev, (unsigned char *)cmd, sizeof(cmd), (unsigned char *)buf, sizeof(buf)) != 0)
		{
			fprintf (stderr, "Error: getfile failed during transfer - %s\n", strerror(errno));
			fclose (fd);
			return -1;
		}

		bytes_left = size_to_long (files[idx].size) - bytes_written;
		if (verbose)
			fprintf (stdout, "Bytes left: %li\n", bytes_left);
		if (bytes_left <= 0)
		{
			if (verbose)
				fprintf (stdout, "Transfer of %s complete\n", filename);
			break;
		}

		//Check to see if we are on the last chunk
		if (bytes_left < MAX_DATA_LEN)
		{
			bytes_written += fwrite (buf, sizeof(unsigned char), bytes_left, fd);
			break;
		}
		//Otherwise write the chunk and move onto the next one
		bytes_written += fwrite (buf, sizeof(unsigned char), MAX_DATA_LEN, fd);

		//increment the offset
		blk_idx++;
		cmd[2] = (unsigned char)(blk_idx >> 24) & 0xFF;
		cmd[3] = (unsigned char)(blk_idx >> 16) & 0xFF;
		cmd[4] = (unsigned char)(blk_idx >>  8) & 0xFF;
		cmd[5] = (unsigned char)(blk_idx      ) & 0xFF;
	}
	fclose (fd);
	return 0;
}


/** TOOLBOX_MODE_DEVICES (read, length 10)
 * Input:
 *  CDB 00 = command byte
 * Output:
 *  8 bytes, each indicating the device type of the emulated SCSI devices
 *           or 0xFF for not-enabled targets
 */
static int bluescsi_listdevices(int dev, char **outbuf)
{
	char cmd[10] = {BLUESCSI_TOOLBOX_MODE_DEVICES, 0, 0, 0, 0, 0, 0, 0, 0, 0};
	char buf[8];
	*outbuf = NULL;

	memset(buf, 0, sizeof(buf));
	if (scsi_send_command(dev, (unsigned char *)cmd, sizeof(cmd), (unsigned char *)buf, sizeof(buf)) != 0)
	{
		fprintf (stderr, "Error: BlueSCSI listdevices failed - %s\n", strerror(errno));
		return -1;
	}
	*outbuf = (char *)calloc(sizeof(buf), sizeof(char));
	if (*outbuf) {
		memcpy(*outbuf, buf, sizeof(buf));
	}
	return 0;
}
//Returns zero on success
static int bluescsi_inquiry(int dev, int print)
{
	char cmd[] ={SCSI_INQUIRY, 0, 0, 0, sizeof(scsi_inquiry), 0};	
	char buf[sizeof(scsi_inquiry)];
	const char *BlueSCSI_ID = "BlueSCSI";
	scsi_inquiry inq;

	memset(buf, 0, sizeof(buf));
	if (scsi_send_command(dev, (unsigned char *)cmd, sizeof(cmd), (unsigned char *)buf, sizeof(buf)) != 0)
	{
		fprintf (stderr, "Error: inquiry command failed - %s\n", strerror(errno));
		return 1;
	}
	memset (&inq, 0, sizeof(scsi_inquiry));
	memcpy (&inq.version, &buf[2], 1);
	memcpy (&inq.vendor_id, &buf[8], sizeof(inq.vendor_id) - 1);
	inq.vendor_id[9] = '\0';
	memcpy (&inq.product_id, &buf[16], sizeof(inq.product_id) - 1);
	inq.product_id[17] = '\0';
	memcpy (&inq.product_rev, &buf[32], sizeof(inq.product_rev) - 1);
	inq.product_rev[32] = '\0';

	if (verbose || print)
	{
		fprintf (stdout, "SCSI version: %i\n", inq.version);
		fprintf (stdout, "vendor_id: %s \nproduct_id: %s\n", inq.vendor_id, inq.product_id);
		fprintf (stdout, "product_rev: %s\n", inq.product_rev);
		fprintf (stdout, "debug mode: %i\n", bluescsi_getdebug(dev));
	}
	//TODO Once a BlueSCSI drive is found, send a MODE SENSE 0x1A command for page 0x31. Validate it against the BlueSCSIVendorPage (see: mode.c)
	if (strstr (inq.product_rev, BlueSCSI_ID) != NULL)
		return 0;
	else
	{
		fprintf (stderr, "Error: didn't find ID %s in product_rev\n", BlueSCSI_ID);
		return 2;
	}
}

static void do_drive(char *path, int list, int verbose, int cd_img, int file, char *outdir)
{
	int dev;
	int dev_path_num; //SCSI ID pulled from path
	int type[8];
	int i;
	char *inq = NULL;
	int readonly; //Needed to determine if it's a CDROM and only able to be opened READONLY

	if (list == MODE_CD)
	       readonly = 1;

	dev = scsi_open(path, readonly);
	if (dev < 0) {
		fprintf(stderr, "ERROR: Cannot open device: %s\n Try running again as root\n", strerror(errno));
		exit(1);
	}
	if (verbose)	
		printf("Opened dev %i %s:\n", dev, path);

	//Do inquiry to check we are working with a BlueSCSI
	if (bluescsi_inquiry (dev, PRINT_OFF) != 0)
	{
		fprintf (stderr, "Didn't find a BlueSCSI device at %s\n", path);
		scsi_close (dev);
		exit(1);
	}
	
	//Next double check what kind of device we are emulating
	if (bluescsi_listdevices(dev, &inq) == 0) {
		if (verbose)
			fprintf (stdout, "List device flags: ");
		for (i = 0; i < 8;i++)
		{
			type[i] = inq[i];
			if (verbose)
				fprintf (stdout, "%02x ", inq[i]);
		}
		if (verbose)
			fprintf (stdout, "\n");
		free(inq);
	}

	if ((dev_path_num = path_to_devnum(path)) < 0)
		goto close_dev;

	if (verbose)
		fprintf(stdout, "dev_path_num %i\n", dev_path_num);

	if (list == MODE_CD)
		bluescsi_listcds(dev);
	else if (list == MODE_INQUIRY)
		bluescsi_inquiry(dev, PRINT_ON);
	else if (list == MODE_DEBUG)
		bluescsi_setdebug(dev, file);
	else if (list == MODE_SHARED)
		bluescsi_listfiles(dev, PRINT_ON);
	else if (list == MODE_PUT)
		bluescsi_sendfile (dev, outdir);
	else if (file != -1)
		bluescsi_getfile (dev, file, outdir);
	else if (cd_img != -1)
	{
		if (type[dev_path_num] != TYPE_CD)
			fprintf (stderr, "Device doesn't seem to be a CD drive?\n");
		else
			bluescsi_setnextcd(dev, cd_img);
	}

close_dev:
	scsi_close(dev);
}

static void usage(void)
{
	fprintf(stderr, "\nUsage:   bstoolbox [options] [device]\n\n");
	fprintf(stderr, "Example: bstoolbox -s /dev/scsi/sc0d1l0\n\n");
	fprintf(stderr, "Options:\n");
	fprintf(stderr, "\t-h      : display this help message and exit\n");
	fprintf(stderr, "\t-v      : be verbose\n");
	fprintf(stderr, "\t-i      : interrogate BlueSCSI and return version\n");
	fprintf(stderr, "\t-l      : list available CDs\n");
	fprintf(stderr, "\t-s      : List /shared directory\n");
	fprintf(stderr, "\t-c num  : change to CD number (1, 2, etc)\n");
	fprintf(stderr, "\t-g num  : get file from shared directory (1, 2, etc)\n");
	fprintf(stderr, "\t-p file : put file to shared directory\n");
	fprintf(stderr, "\t-o dir  : set output directory, defaults to current\n");
	fprintf(stderr, "\t-d num  : set debug mode (0 = off, 1 - on)\n");
	fprintf(stderr, "\n\nPlease make sure you run the program as root.\n");
}

static int mediad_start(void) {
    int status;

    // Starting mediad service
    if (verbose)
    	fprintf (stdout, "Starting mediad...\n");
    status = system("/etc/init.d/mediad start");
    if (status != 0) {
        fprintf(stderr, "Failed to start mediad service: %s\n", strerror(errno));
        return 1;
    }
    return 0;
}
static int mediad_stop(void) {
    int status;

    // Stop mediad service
    if (verbose)
    	fprintf (stdout, "Stopping mediad...\n");
    status = system("/etc/init.d/mediad stop");
    if (status != 0) {
        fprintf(stderr, "Failed to stop mediad service: %s\n", strerror(errno));
        return 1;
    }
    return 0;
}

int main(int argc, char *argv[])
{
	int c, cdimg = -1, list = 0, file = -1;
	char outdir[1024];

	while ((c = getopt(argc, argv, "hvlsic:d:g:o:p:")) != -1) switch (c) {
		case 'c':
			cdimg = atoi(optarg);
			break;
		case 'g':
			file = atoi(optarg);
			break;
		case 'o':
			strncpy(outdir, optarg, sizeof(outdir) - 1);
			break;
		case 'p':
			strncpy(outdir, optarg, sizeof(outdir) - 1);
			list = MODE_PUT;
			break;
		case 'l':
			list = MODE_CD;
			break;
		case 's':
			list = MODE_SHARED;
			break;
		case 'i':
			list = MODE_INQUIRY;
			break;
		case 'd':
			list = MODE_DEBUG;
			file = atoi(optarg);
			break;
		case 'v':
			verbose = 1;
			break;
		case 'h':
		default:
			usage();
			return 1;
	}
	
	argc -= optind;
	argv += optind;
	
	if (cdimg != -1)
		mediad_stop (); //Stop the mediad before changing image

	if (argc < 1) {
		fprintf (stderr, "Please specify device (\"/dev/scsi/...\"\n");
		usage();
		return 0;
	} else if (argc > 1) {
		fprintf(stderr, "WARNING: Options after '%s' ignored.\n", argv[0]);
	}
	do_drive(argv[0], list, verbose, cdimg, file, outdir);
	if (cdimg != -1)
		mediad_start ();
	
	return 0;
}

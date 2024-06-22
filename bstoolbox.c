/*
 * BlueSCSI v2 IRIX tools
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/dsreq.h>
#include <invent.h>

#define SCSI_INQUIRY    0x12
#define BLUESCSI_TOOLBOX_COUNT_FILES    0xD2
#define BLUESCSI_TOOLBOX_LIST_FILES     0xD0
#define BLUESCSI_TOOLBOX_GET_FILE       0xD1
#define BLUESCSI_TOOLBOX_SEND_FILE_PREP 0xD3
#define BLUESCSI_TOOLBOX_SEND_FILE_10   0xD4
#define BLUESCSI_TOOLBOX_SEND_FILE_END  0xD5
#define BLUESCSI_TOOLBOX_TOGGLE_DEBUG   0xD6
#define BLUESCSI_TOOLBOX_LIST_CDS       0xD7
#define BLUESCSI_TOOLBOX_SET_NEXT_CD    0xD8
#define BLUESCSI_TOOLBOX_LIST_DEVICES   0xD9
#define BLUESCSI_TOOLBOX_COUNT_CDS      0xDA
#define OPEN_RETRO_SCSI_TOO_MANY_FILES 0x0001

#define MAX_FILES 100 //TODO Hmmmm
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

static int scsi_open(char *path)
{
	return open(path, O_RDONLY | O_SYNC);
}


static int scsi_close(int dev)
{
	return close(dev);
}


static int scsi_send_command(int dev, char *cmd, int cmd_len, char *buf, int buf_len)
{
	int i;
	int try;
	dsreq_t r;
	memset(&r, 0, sizeof(dsreq_t));
	
	/* Assemble the request structure */
	r.ds_cmdbuf   = (caddr_t) cmd;
	r.ds_cmdlen   = cmd_len;
	r.ds_databuf  = (caddr_t) buf;
	r.ds_datalen  = buf_len;
	r.ds_sensebuf = (caddr_t) buf;
	r.ds_senselen = buf_len;
	r.ds_time     = 5 * 1000;  /* 5 seconds should be enough */
	r.ds_flags    = DSRQ_READ;
	
	if (verbose){
		fprintf(stdout, "Sending SCSI command: ");
		for (i = 0; i < cmd_len; ++i) {
			fprintf(stdout, "%02x ", (unsigned char)r.ds_cmdbuf[i]);
		}
		fprintf(stdout, "\n");
	}	
	/* Issue the request */
	//if (ioctl(dev, DS_ENTER, &r))
	//	return -errno;
	for (try = 0; try < 10; try ++){
		if (ioctl(dev, DS_ENTER, &r) < 0 || r.ds_status != 0){
			fprintf(stderr, "WARNING: SCSI command timed out (%d); retrying...\n", r.ds_status);
			sleep(try + 1);
		}
		else
		  break;
		if (try >= 10){
			fprintf(stderr, "ERROR: Unable to send print data (%d)\n",r.ds_status);
			return (1);
		}
	}
	return 0;
}


//Returns the number of files in the /shared directory
static int bluescsi_countfiles(int dev)
{
	char cmd[10] = {BLUESCSI_TOOLBOX_COUNT_FILES, 0, 0, 0, 0, 0, 0, 0, 0, 0};	
	char buf[1];
	int ret;
	memset(buf, 0, sizeof(buf));
	if (scsi_send_command(dev, cmd, sizeof(cmd), buf, sizeof(buf)) != 0)
	{
		fprintf (stderr, "Error: BlueSCSI test failed - %s\n", strerror(errno));
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
	if (scsi_send_command(dev, cmd, sizeof(cmd), buf, sizeof(buf)) != 0)
	{
		fprintf (stderr, "Error: BlueSCSI test failed - %s\n", strerror(errno));
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
	if (scsi_send_command(dev, cmd, 10, NULL, 0) != 0)
	{
		fprintf (stderr, "Error: setnextcd failed - %s\n", strerror(errno));
		return -1;
	}
	return 0;
}

static int bluescsi_listcds(int dev)
{
	char cmd[10] = {BLUESCSI_TOOLBOX_LIST_CDS, 0, 0, 0, 0, 0, 0, 0, 0, 0};
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
	if (scsi_send_command(dev, cmd, sizeof(cmd), buf, buf_size) != 0)
	{
		fprintf (stderr, "Error: BlueSCSI test failed - %s\n", strerror(errno));
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

static int bluescsi_listfiles(int dev)
{
	char cmd[10] = {BLUESCSI_TOOLBOX_LIST_FILES, 0, 0, 0, 0, 0, 0, 0, 0, 0};
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
	if (scsi_send_command(dev, cmd, sizeof(cmd), buf, buf_size) != 0)
	{
		fprintf (stderr, "Error: listfiles failed - %s\n", strerror(errno));
		return -1;
	}
	//Copy SCSI data to global files var	
	for (i = 0; i < num_files; i++) {
		memcpy(&files[i], buf + i * sizeof(ToolboxFileEntry), sizeof(ToolboxFileEntry));
	}
	if (verbose)
	{	
		for (i = 0;i < num_files;i++)
			fprintf (stdout, "#%i %s %li bytes\n", files[i].index, files[i].name, size_to_long(files[i].size));
	}
	return 0;
}
#define MAX_DATA_LEN 4096

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
	if (bluescsi_listfiles (dev) != 0)
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
		if (scsi_send_command(dev, cmd, sizeof(cmd), buf, sizeof(buf)) != 0)
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


/** TOOLBOX_LIST_DEVICES (read, length 10)
 * Input:
 *  CDB 00 = command byte
 * Output:
 *  8 bytes, each indicating the device type of the emulated SCSI devices
 *           or 0xFF for not-enabled targets
 */
static int bluescsi_listdevices(int dev, char **outbuf)
{
	char cmd[10] = {BLUESCSI_TOOLBOX_LIST_DEVICES, 0, 0, 0, 0, 0, 0, 0, 0, 0};
	char buf[8];
	*outbuf = NULL;

	memset(buf, 0, sizeof(buf));
	if (scsi_send_command(dev, cmd, sizeof(cmd), buf, sizeof(buf)) != 0)
	{
		fprintf (stderr, "Error: BlueSCSI test failed - %s\n", strerror(errno));
		return -1;
	}
	*outbuf = (char *)calloc(sizeof(buf), sizeof(char));
	if (*outbuf) {
		memcpy(*outbuf, buf, sizeof(buf));
	}
	return 0;
}
//Returns zero on success
static int bluescsi_inquiry(int dev)
{
	char cmd[] ={SCSI_INQUIRY, 0, 0, 0, sizeof(scsi_inquiry), 0};	
	char buf[sizeof(scsi_inquiry)];
	const char *BlueSCSI_ID = "BlueSCSI";
	scsi_inquiry inq;

	memset(buf, 0, sizeof(buf));
	if (scsi_send_command(dev, cmd, sizeof(cmd), buf, sizeof(buf)) != 0)
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
	inq.product_rev[33] = '\0';
	
	if (verbose)
	{
		fprintf (stdout, "SCSI version: %i\n", inq.version);
		fprintf (stdout, "vendor_id: %s \nproduct_id: %s\n", inq.vendor_id, inq.product_id);
		fprintf (stdout, "product_rev: %s\n", inq.product_rev);
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

enum {
	TYPE_NONE = 255,
	TYPE_HDD = 0x00,
	TYPE_CD = 0x02
} dev_type;

enum {
	LIST_NONE, 
	LIST_CD,
	LIST_SHARED
};

static void do_drive(char *path, int list, int verbose, int cd_img, int file, char *outdir)
{
	int   dev;
       	int dev_path_num; //SCSI ID pulled from path
	int type[8];
	int i;
	char *inq = NULL;

	dev = scsi_open(path);
	if (dev < 0) {
		fprintf(stderr, "ERROR: Cannot open device: %s\n", strerror(errno));
		exit(1);
	}
	if (verbose)	
		printf("Opened dev %i %s:\n", dev, path);

	//Do inquiry to check we are working with a BlueSCSI
	if (bluescsi_inquiry (dev) != 0)
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
	// Extract the device number from the path 
	// TODO This will probably break very easily
    	if (sscanf(path, "/dev/scsi/sc%*dd%dl%*d", &dev_path_num) != 1) {
        	fprintf(stderr, "ERROR: Invalid path format: %s\n", path);
        	scsi_close(dev);
        	return;
    	}
	if (verbose)
    		fprintf(stdout, "dev_path_num %i\n", dev_path_num);

	if (list == LIST_CD)
		bluescsi_listcds(dev);
	else if (list == LIST_SHARED)
		bluescsi_listfiles(dev);
	else if (file != -1)
		bluescsi_getfile (dev, file, outdir);
	else if (cd_img != -1)
	{
		if (type[dev_path_num] != TYPE_CD)
			fprintf (stderr, "Device doesn't seem to be a CD drive?\n");
		else
			bluescsi_setnextcd(dev, cd_img);
	}
	scsi_close(dev);
}

static void usage(void)
{
	fprintf(stderr, "\nUsage:   bstoolbox [-h] [-v] [-l] [-c/-g num] [-o directory] [device]\\nn");
	fprintf(stderr, "Example: bstoolbox -l /dev/scsi/sc0d1l0\n\n");
	fprintf(stderr, "Options:\n");
	fprintf(stderr, "\t-h      : display this help message and exit\n");
	fprintf(stderr, "\t-v      : be verbose\n");
	fprintf(stderr, "\t-l      : list available CDs\n");
	fprintf(stderr, "\t-s      : List /shared directory\n");
	fprintf(stderr, "\t-c num  : change to CD number (1, 2, etc)\n");
	fprintf(stderr, "\t-g num  : get file from shared directory (1, 2, etc)\n");
	fprintf(stderr, "\t-o dir  : set output directory, defaults to current\n");
	fprintf(stderr, "\n\nPlease make sure you run the program as root.\n");
}

static int mediad_start() {
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
static int mediad_stop() {
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

	while ((c = getopt(argc, argv, "hvlsc:g:o:")) != -1) switch (c) {
		case 'c':
			cdimg = atoi(optarg);
			break;
		case 'g':
			file = atoi(optarg);
			break;
		case 'o':
			strncpy(outdir, optarg, sizeof(outdir) - 1);
			break;
		case 'l':
			list = LIST_CD;
			break;
		case 's':
			list = LIST_SHARED;
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

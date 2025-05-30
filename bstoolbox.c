/*
 * BlueSCSI v2 IRIX and Linux toolbox
 */
#include "bstoolbox.h"

/*
 * Sending Files
 * Sending files from the Host to the SD card is a three-step process:
 *
 * BLUESCSI_TOOLBOX_SEND_FILE_PREP 0xD3 to prepare a file on the SD card for receiving.
 * BLUESCSI_TOOLBOX_SEND_FILE_10 0xD4 to send the actual data of the file.
 * BLUESCSI_TOOLBOX_SEND_FILE_END 0xD5 to close the file.
 */

/*
 * LUESCSI_TOOLBOX_SEND_FILE_PREP 0xD3
 * Prepares a file on the SD card in the ToolBoxSharedDir (Default /shared) for receiving.
 *
 * The file name is 33 char name sent in the SCSI data, null terminated. The name should only contain valid characters for file names on FAT32/ExFAT.
 *
 * If the file is not able to be created a CHECK_CONDITION ILLEGAL_REQUEST is set as the sense.
 *
 * BLUESCSI_TOOLBOX_SEND_FILE_10 0xD4
 * Receive data from the host in blocks of 512 bytes.
 *
 * CDB[1..2] - Number of bytes sent in this request. Big endian. Minimum 1, maximum 512.
 *
 * CDB[3..5] - Block number in the file for these bytes. Big endian.
 *
 * If the file has a write error sense will be set as CHECK_CONDITION ILLEGAL_REQUEST. You may try to resend the block or fail and call BLUESCSI_TOOLBOX_SEND_FILE_END
 *
 * NOTE: The number of bytes sent should be 512 in all but the final block of the file.
 *
 * BLUESCSI_TOOLBOX_SEND_FILE_END 0xD5
 * Once the file is completely sent this command will close the file.
 */
static int bluescsi_sendfile(int dev, char *path)
{
	char cmd[10] = { BLUESCSI_TOOLBOX_SEND_FILE_PREP, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	char filename[NAME_BUF_SIZE];
	char *base_name;
	char send_buf[SEND_BUF_SIZE];
	long int bytes_read = 0;
	long int actual_read = 0;
	int chunk;
	int buf_idx = 0;
	int ret;
	FILE *fd;
	long int filesize;
	struct stat st;

	if (verbose)
		fprintf(stdout, "sendfile: %s\n", path);

	memset(send_buf, 0, sizeof(send_buf));
	//TODO Copnsider using basename()
	// Extract base filename
	base_name = strrchr(path, '/');
	if (base_name == NULL) {
		base_name = path;
	} else {
		base_name++; // skip the slash
	}

	if (strlen(base_name) >= NAME_BUF_SIZE) {
		fprintf(stderr, "Error: sendfile Filename too long: %s\n", base_name);
		return -1;
	}

	memset(filename, 0, NAME_BUF_SIZE);
	strncpy(filename, base_name, NAME_BUF_SIZE - 1);

	// Open file
	fd = fopen(path, "rb");
	if (fd == NULL) {
		fprintf(stderr, "Error: sendfile couldn't open %s\n", path);
		return 1;
	}

	if (stat(path, &st) == 0) {
		if (verbose)
			printf("File size of %s is %lld bytes\n", filename, (long long)st.st_size);
	} else {
		fprintf(stderr, "Error: sendfile couldn't stat %s\n", path);
		fclose(fd);
		return 1;
	}
	filesize = st.st_size;

	// Send filename
	if (scsi_send_commandw(dev, (unsigned char *)cmd, SCSI_CMD_LENGTH, (unsigned char *)filename, 33) != 0) {
		fprintf(stderr, "Error: sendfileprep failed - %s\n", strerror(errno));
		fclose(fd);
		return 1;
	}

	// Prepare to send file data
	cmd[0] = BLUESCSI_TOOLBOX_SEND_FILE_10;

	while (bytes_read < filesize) {
		chunk = (filesize - bytes_read) < SEND_BUF_SIZE ? (filesize - bytes_read) : SEND_BUF_SIZE;
		memset(send_buf, 0, SEND_BUF_SIZE);

		actual_read = fread(send_buf, 1, chunk, fd);
		if (actual_read <= 0) {
			fprintf(stderr, "Error: fread failed or returned 0 at offset %ld\n", bytes_read);
			fclose(fd);
			return 1;
		}

		cmd[1] = (actual_read & 0xFF00) >> 8;
		cmd[2] = (actual_read & 0xFF);

		cmd[3] = (buf_idx & 0xFF0000) >> 16;
		cmd[4] = (buf_idx & 0xFF00) >> 8;
		cmd[5] = (buf_idx & 0xFF);

		ret = scsi_send_commandw(dev, (unsigned char *)cmd, sizeof(cmd), (unsigned char *)send_buf, actual_read);
		if (ret != 0) {
			fprintf(stderr, "Error: sendfile10 failed - %s\n", strerror(errno));
			fclose(fd);
			return 1;
		}

		bytes_read += actual_read;
		buf_idx++;
	}

	// Send file end command
	memset(cmd, 0, sizeof(cmd));
	cmd[0] = BLUESCSI_TOOLBOX_SEND_FILE_END;

	if (scsi_send_command(dev, (unsigned char *)cmd, sizeof(cmd), NULL, 0) != 0) {
		fprintf(stderr, "Error: sendfileend failed - %s\n", strerror(errno));
		fclose(fd);
		return 1;
	}

	fclose(fd);
	return 0;
}


/*
 * BLUESCSI_TOOLBOX_TOGGLE_DEBUG 0xD6
 * Enable or disable Debug logs. Also allows you to get the current status.
 *
 * If CDB[1] is set to 0 it is the subcommand Set Debug. The value of CDB[2] is used as the boolean value for the debug flag.
 *
 * If CDB[1] is set to 1 it is the subcommand Get Debug. The boolean value is sent as a 1 byte value.
 *
 * NOTE: Debug logs significantly decrease performance while enabled. When your app enables debug you MUST notify them of the decreased performance.
 */

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

/*
 * BLUESCSI_TOOLBOX_COUNT_FILES 0xD2
 * Counts the number of files in the ToolBoxSharedDir (default /shared). The purpose is to allow the host program to know how much data will be sent back by the List files function.
 *
 * This can be sent to any valid BlueSCSI target.
 */

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

	if (num < 0 || num > max_cds)
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
/*
 * BLUESCSI_TOOLBOX_LIST_CDS 0xD7
 * Lists all the files for the current directory for the selected SCSI ID target. Eg: When selecting SCSI ID 3 it will look for a CD3 folder and list files from there. The structure is ToolboxFileEntry.
 *
 * NOTE: Since there is no universal name for a CD image there is no filtering done on the lists of files.
 */
static int bluescsi_listcds(int dev)
{
	char cmd[10] = {BLUESCSI_TOOLBOX_MODE_CDS, 0, 0, 0, 0, 0, 0, 0, 0, 0};
	char *buf;
	int i, j;
	int buf_size;
	int num_cds;

	num_cds = bluescsi_countcds (dev);
	if (num_cds < 0 || num_cds > MAX_FILES)
	{
		fprintf (stderr, "Error:  CD number requested invalid: %i\n", num_cds);
		return -1;
	}
	fprintf (stdout, "Found %i CDs\n", num_cds);
	buf_size = sizeof(ToolboxFileEntry) * num_cds;
	
	buf = (char *)malloc(buf_size);
	if (buf == NULL)
	{
		fprintf (stderr, "Error: failed to malloc %i bytes: - %s\n", buf_size, strerror(errno));
		return -1;
	}

	memset(buf, 0, buf_size);
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

//Helper function to convert 40bit size into a long
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

/*
 * Receiving Files
 * The Host machine can read files directly off the SD card and write them to the Host.
 *
 * To receive files
 *
 * Count and list the files with BLUESCSI_TOOLBOX_COUNT_FILES 0xD2 and BLUESCSI_TOOLBOX_LIST_FILES 0xD0.
 * Use the file index and byte offset to transfer the desired file with BLUESCSI_TOOLBOX_GET_FILE 0xD1.
 *
 */


/*
 * BLUESCSI_TOOLBOX_LIST_FILES 0xD0
 * Returns a list of files in the ToolBoxSharedDir (Default /shared) in a ToolboxFileEntry struct
 * NOTE: File names are truncated to 32 chars - but can still be transferred. 
 * NOTE: You may need to convert characters that are valid file names on FAT32/ExFat to support the hosts native character encoding and file name limitations. 
 * NOTE: Currently the response is limited to 100 entries, Will return 
 * NOTE: BlueSCSI only transfers as many entries as are actually present. You should request the file count first, then size your receive buffer to match that number of entries.
 */
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
	if (num_files < 0 || num_files > MAX_FILES)
	{
		fprintf (stderr, "Error: listfiles num_files invalid: %i", num_files);
		return -1;
	}
	files_count = num_files;
	if (verbose)
		fprintf (stdout, "Found %i files\n", num_files);
	buf_size = sizeof(ToolboxFileEntry) * num_files;
	
	buf = (char *)malloc(buf_size);
	if (buf == NULL)
	{
		fprintf (stderr, "Error: failed to malloc %i bytes: - %s\n", buf_size, strerror(errno));
		return -1;
	}

	memset(buf, 0, buf_size);
	if (scsi_send_command(dev, (unsigned char *)cmd, sizeof(cmd), (unsigned char *)buf, buf_size) != 0)
	{
		fprintf (stderr, "Error: listfiles failed - %s\n", strerror(errno));
		return -1;
	}
	//Copy SCSI data to global files var	
	for (i = 0; i < num_files; i++) {
		memcpy(&files[i], buf + i * sizeof(ToolboxFileEntry), sizeof(ToolboxFileEntry));
		files[i].name[sizeof(files[i].name) - 1] = '\0';
	}
	if (verbose || print)
	{	
		for (i = 0;i < num_files;i++)
			fprintf (stdout, "#%i %s %li bytes\n", files[i].index, files[i].name, size_to_long(files[i].size));
	}
	return 0;
}
/*
 * BLUESCSI_TOOLBOX_GET_FILE 0xD1
 * Transfers a file from the ToolBoxSharedDir (Default /shared) to the Host computer.
 *
 * CDB[1] contains the index of the file to transfer.
 *
 * CDB[2..5] contains the offset in the file in 4096 byte blocks. Big endian.
 */

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
	if (filename == NULL)
	{
		fprintf (stderr, "Error mallocing filename\n");
		return -1;
	}

	fprintf (stdout, "Fetching %s\n", files[idx].name);
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
	bytes_written = 0;
	bytes_left = 0;

	//Read the data from the SCSI bus and store to disk 
	//TODO timeouts and sanity checks
	while (1)
	{	
		if (scsi_send_command(dev, (unsigned char *)cmd, sizeof(cmd), (unsigned char *)buf, MAX_DATA_LEN) != 0)
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
		return 0;
	}
	else
		return -1;
}

//Interrogate the device and find out it's capabilties
static int bluescsi_inquiry(int dev, int print)
{
	char cmd[] ={SCSI_INQUIRY, 0, 0, 0, sizeof(scsi_inquiry), 0};	
	char buf[sizeof(scsi_inquiry)];
	const char *BlueSCSI_ID = "BlueSCSI";
	scsi_inquiry inq;
	int i;
	char* dev_flags;
	int additional_len;
	int total_len;
	int toolbox_api_version;

	memset(buf, 0, sizeof(buf));
	if (scsi_send_command(dev, (unsigned char *)cmd, sizeof(cmd), (unsigned char *)buf, sizeof(buf)) != 0)
	{
		fprintf (stderr, "Error: inquiry command failed - %s\n", strerror(errno));
		return 1;
	}
	//Clear and fill the buffer with the inquiry data
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
	// Print the Toolbox API version if the extended data is present
	additional_len = buf[4]; //offset 4 contains how much extra data is in the packet
	total_len = additional_len + 5;

	if (total_len <= sizeof(buf)) {
		toolbox_api_version = buf[total_len - 1];
		if (verbose)
			fprintf(stdout, "Toolbox API version: %u\n", toolbox_api_version);

		if (toolbox_api_version < BLUESCSI_TOOLBOX_API_VER) {
			fprintf(stdout, "Toolbox API version %u too old, expecting: %u\n", toolbox_api_version, BLUESCSI_TOOLBOX_API_VER);
			//return -1;
		}

	} else {
		fprintf(stdout, "Toolbox API version: not available (length mismatch)\n");
	}

	//Get the 8 byte device flags to see what type it is
	if (bluescsi_listdevices(dev, &dev_flags) == 0) {
		if (verbose)
			fprintf (stdout, "Device flags: ");
		for (i = 0; i < 8; i++)
		{
			device_list[i] = dev_flags[i]; //Write the falgs to the device list
			if (verbose)
				fprintf (stdout,"%02x ", (unsigned char) dev_flags[i]);
		}
		if (verbose)
			fprintf(stdout, "\n");
		free(dev_flags);


	}
	else {
		fprintf (stderr, "Failed to fetch device flags with bluescsi_listdevices(): %s\n", strerror(errno));
		free(dev_flags);
		return 1;
	}

	//TODO Once a BlueSCSI drive is found, send a MODE SENSE 0x1A command for page 0x31. Validate it against the BlueSCSIVendorPage (see: mode.c)
	if (strstr (inq.product_rev, BlueSCSI_ID) != NULL)
		return 0; //TODO FIX FIX FIX HDD 
	else
	{
		fprintf (stderr, "Error: didn't find ID %s in product_rev\n", BlueSCSI_ID);
		return 1;
	}
}

static void do_drive(char *path, int list, int verbose, int cd_img, int file, char *outdir)
{
	int dev;
	int dev_scsi_id; //SCSI ID pulled from path
	int readonly; //Needed to determine if it's a CDROM and only able to be opened READONLY
	readonly = 0;
	
	//Open the device read only if we are attempting a CD operation
	if (list == MODE_CD || cd_img != NOT_ACTIVE) //TODO Probably need to detect more modes here
	       readonly = 1;

	dev = scsi_open(path, readonly);
	if (dev < 0) {
		if (!readonly)
		{
			fprintf (stderr, "Error opening device for read/write, trying to open readonly\n");
			dev = scsi_open(path, 1); //Try to open the device as read only
			
		}
		if (dev < 0) {
			fprintf(stderr, "ERROR: Cannot open device: %s\nTry running again as root\n", strerror(errno));
			exit(1);
		}
	}

	//Do inquiry to check we are working with a BlueSCSI
	//device_type = bluescsi_inquiry (dev, PRINT_OFF);
	if (bluescsi_inquiry (dev, PRINT_OFF) != 0)
	{
		fprintf (stderr, "Didn't find a BlueSCSI device at %s\n", path);
		scsi_close (dev);
		exit(1);
	}
	
	if ((dev_scsi_id = path_to_devnum(path)) < 0)
		goto close_dev;

	if (list == MODE_CD)
	{
		if (device_list[dev_scsi_id] != TYPE_CD)
		{
			fprintf (stderr, "Tried to list CDs, but an emulated CD drive wasn't detected\n");
			scsi_close(dev);
			exit(1);
		}
		else
			bluescsi_listcds(dev);
	}
	else if (list == MODE_INQUIRY)
		bluescsi_inquiry(dev, PRINT_ON);
	else if (list == MODE_DEBUG)
		bluescsi_setdebug(dev, file);
	else if (list == MODE_SHARED)
		bluescsi_listfiles(dev, PRINT_ON);
	else if (list == MODE_PUT)
		bluescsi_sendfile (dev, outdir);
	else if (file != NOT_ACTIVE)
		bluescsi_getfile (dev, file, outdir);
	else if (cd_img != NOT_ACTIVE)
	{
		if (device_list[dev_scsi_id] != TYPE_CD)
			fprintf (stderr, "Device doesn't seem to be a CD drive? Detected type %i on SCSI ID %i\n", device_list[dev_scsi_id], dev_scsi_id);
		else
			bluescsi_setnextcd(dev, cd_img);
	}

close_dev:
	scsi_close(dev);
}

static void usage(void)
{
	fprintf(stderr, "\nUsage:   bstoolbox [options] [device]\n\n");
#if defined(OS_IRIX)
	fprintf(stderr, "example: bstoolbox -s /dev/scsi/sc0d1l0\n\n");
#elif defined(OS_LINUX)
	fprintf(stderr, "example: bstoolbox -s /dev/sg2\n\n");
#endif
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

int main(int argc, char *argv[])
{
	int c, cdimg = NOT_ACTIVE, list = 0, file = NOT_ACTIVE;
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
	//Stop any removable media managers running on the host system before changing CDs
	if (cdimg != -1)
		mediad_stop ();

	if (argc < 1) {
		fprintf (stderr, "No device path entered\n");
		usage();
		return 1;
	} else if (argc > 1) {
		fprintf(stderr, "WARNING: Options after '%s' ignored.\n", argv[0]);
	}
	//strcpy (device_path, argv[0]); //Copy the path for later
	do_drive(argv[0], list, verbose, cdimg, file, outdir);
	
	if (cdimg != -1)
		mediad_start ();
	
	return 0;
}

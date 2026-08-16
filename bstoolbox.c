/*
 * BlueSCSI v2 IRIX and Linux toolbox
 */
#include "bstoolbox.h"

int device_list[8];
int verbose = 0;
ToolboxFileEntry files[MAX_FILES];
int files_count = 0;

static int bluescsi_listfiles(int dev, int print);
static int bluescsi_getfile(int dev, int idx, char *outdir);

static unsigned long long size_to_long(const unsigned char size[5])
{
        int i;
        unsigned long long result = 0;
        for (i = 0; i < 5; i++)
        {
                result = (result << 8) | size[i];
        }
        return result;
}

/*
 * BLUESCSI_TOOLBOX_METADATA (0xD9) Subcommands
 */

/* Subcommand 0x00 - List Devices */
static int bluescsi_metadata_list_devices(int dev, unsigned char dev_map[8])
{
	unsigned char cmd[10];
	unsigned char buf[8];
	int i;

	memset(cmd, 0, sizeof(cmd));
	cmd[0] = BLUESCSI_TOOLBOX_METADATA;
	cmd[1] = BLUESCSI_TOOLBOX_METADATA_LIST_DEVICES;
	cmd[8] = 0x08; /* Allocation length = 8 bytes */

	memset(buf, 0xFF, sizeof(buf));

	if (scsi_send_command(dev, cmd, sizeof(cmd), buf, sizeof(buf)) != 0)
	{
		if (verbose)
			fprintf(stderr, "Error: metadata list_devices command failed - %s\n", strerror(errno));
		return -1;
	}

	for (i = 0; i < 8; i++)
	{
		dev_map[i] = buf[i];
	}

	return 0;
}

/* Subcommand 0x01 - Get Capabilities */
static int bluescsi_metadata_get_capabilities(int dev, unsigned char *api_ver, unsigned char *caps)
{
	unsigned char cmd[10];
	unsigned char buf[8];

	memset(cmd, 0, sizeof(cmd));
	cmd[0] = BLUESCSI_TOOLBOX_METADATA;
	cmd[1] = BLUESCSI_TOOLBOX_METADATA_GET_CAP;
	cmd[8] = 0x08; /* Allocation length = 8 bytes */

	memset(buf, 0, sizeof(buf));

	if (scsi_send_command(dev, cmd, sizeof(cmd), buf, sizeof(buf)) != 0)
	{
		/* Legacy devices return CHECK_CONDITION: default to API v0 and no capabilities */
		if (verbose)
			fprintf(stdout, "Metadata get_capabilities unsupported, assuming legacy device (API v0, no caps)\n");
		*api_ver = 0;
		*caps = 0;
		return 0;
	}

	*api_ver = buf[0];
	*caps = buf[1];

	if (verbose)
	{
		fprintf(stdout, "Toolbox Metadata API Version: %u\n", *api_ver);
		fprintf(stdout, "Capability Flags: 0x%02X\n", *caps);
		fprintf(stdout, " - CAP_LARGE_TRANSFERS: %s\n", (*caps & 0x01) ? "Yes" : "No");
		fprintf(stdout, " - CAP_LARGE_SEND     : %s\n", (*caps & 0x02) ? "Yes" : "No");
		fprintf(stdout, " - CAP_SET_WORKING_DIR: %s\n", (*caps & 0x04) ? "Yes" : "No");
	}

	return 0;
}

static int bluescsi_metadata_set_working_dir(int dev, const char *path)
{
	unsigned char cmd[10];
	size_t path_len;
	int ret;

	if (verbose)
		fprintf(stdout, "Setting working directory to: %s\n", path);

	path_len = (path != NULL) ? strlen(path) : 0;
	if (path_len > 64)
	{
		fprintf(stderr, "Error: working directory path exceeds maximum length of 64 bytes\n");
		return -1;
	}
	else if (path_len == 0)
	{
		fprintf(stdout, "Error: set working dir path_len was zero\n");
		return -1;
	}

	memset(cmd, 0, sizeof(cmd));
	cmd[0] = BLUESCSI_TOOLBOX_METADATA;
	cmd[1] = BLUESCSI_TOOLBOX_METADATA_SET_WDIR;
	cmd[8] = (unsigned char)path_len; /* Path length in bytes (DATA_OUT) */


	ret = scsi_send_commandw(dev, cmd, sizeof(cmd), (unsigned char *)path, (int)path_len);
	if (ret != 0)
	{
		fprintf(stderr, "Error: set_working_dir failed - %s\n", strerror(errno));
		return -1;
	}

	return 0;
}

static char *bluescsi_metadata_get_working_dir(int dev)
{
        unsigned char cmd[10];
        unsigned char buf[256];
        char *wdir;
        size_t req_len;
        int ret;

        req_len = 255; /* Cap at 255 bytes so cmd[8] fits and leaves room for '\0' */

        memset(cmd, 0, sizeof(cmd));
        cmd[0] = BLUESCSI_TOOLBOX_METADATA;
        cmd[1] = BLUESCSI_TOOLBOX_METADATA_GET_WDIR;
        cmd[7] = (unsigned char)((req_len >> 8) & 0xFF);
        cmd[8] = (unsigned char)(req_len & 0xFF);

        memset(buf, 0, sizeof(buf));

        ret = scsi_send_command(dev, cmd, sizeof(cmd), buf, (int)req_len);
        if (ret != 0)
        {
                fprintf(stderr, "Error: get_working_dir failed - %s\n", strerror(errno));
                return NULL;
        }

        buf[255] = '\0';

        wdir = (char *)malloc(strlen((char *)buf) + 1);
        if (wdir == NULL)
        {
                fprintf(stderr, "Error: Memory allocation failed for working directory string\n");
                return NULL;
        }

        strcpy(wdir, (char *)buf);

        if (verbose)
                fprintf(stdout, "Current working directory: %s\n", wdir);

        return wdir;
}

static void bluescsi_print_wdir (int dev)
{
	fprintf (stdout, "BlueSCSI working directory: %s\n", bluescsi_metadata_get_working_dir(dev));
}

static int bluescsi_set_wdir (int dev, char *outdir)
{
	return bluescsi_metadata_set_working_dir (dev, outdir);
}

static int bluescsi_get_log(int dev)
{
	char *orig_wdir = NULL;
	char orig_filename[sizeof(files[0].name)];
	int log_idx = -1;
	int i;
	int ret = 0;
	FILE *fd;
	int ch;

	/* 1. Get and store original working directory */
	orig_wdir = bluescsi_metadata_get_working_dir(dev);
	if (orig_wdir == NULL)
	{
		fprintf(stderr, "Error: get_log couldn't determine original working directory\n");
		return -1;
	}

	/* 2. Change working directory to root ("/") */
	if (bluescsi_metadata_set_working_dir(dev, "/") != 0)
	{
		fprintf(stderr, "Error: get_log couldn't change working directory to root\n");
		free(orig_wdir);
		return -1;
	}

	/* 3. List files in root directory to locate "log.txt" */
	if (bluescsi_listfiles(dev, PRINT_OFF) != 0)
	{
		fprintf(stderr, "Error: get_log failed to list root directory files\n");
		ret = -1;
		goto restore_wdir;
	}

	for (i = 0; i < files_count; i++)
	{
		if (strcasecmp(files[i].name, "log.txt") == 0)
		{
			log_idx = files[i].index;
			break;
		}
	}

	if (log_idx == -1)
	{
		fprintf(stderr, "Error: log.txt not found in root directory\n");
		ret = -1;
		goto restore_wdir;
	}

	/* Temporarily rename in memory so bluescsi_getfile writes directly to /tmp/BlueSCSI.log */
	strncpy(orig_filename, files[log_idx].name, sizeof(orig_filename));
	strncpy(files[log_idx].name, "BlueSCSI.log", sizeof(files[log_idx].name) - 1);
	files[log_idx].name[sizeof(files[log_idx].name) - 1] = '\0';

	/* 4. Download directly into "/tmp/BlueSCSI.log" */
	if (bluescsi_getfile(dev, log_idx, "/tmp") != 0)
	{
		fprintf(stderr, "Error: get_log failed to fetch log.txt\n");
		strncpy(files[log_idx].name, orig_filename, sizeof(files[log_idx].name));
		ret = -1;
		goto restore_wdir;
	}

	/* Restore original array entry name */
	strncpy(files[log_idx].name, orig_filename, sizeof(files[log_idx].name));

	/* 5. Print /tmp/BlueSCSI.log contents to stdout */
	fd = fopen("/tmp/BlueSCSI.log", "r");
	if (fd == NULL)
	{
		fprintf(stderr, "Error: get_log couldn't open fetched log file /tmp/BlueSCSI.log\n");
		ret = -1;
		goto restore_wdir;
	}

	while ((ch = fgetc(fd)) != EOF)
	{
		fputc(ch, stdout);
	}
	fclose(fd);

	/* Clean up temporary log file */
	unlink("/tmp/BlueSCSI.log");

restore_wdir:
	/* 6. Restore original working directory */
	if (bluescsi_metadata_set_working_dir(dev, orig_wdir) != 0)
	{
		fprintf(stderr, "Warning: failed to restore working directory to %s\n", orig_wdir);
		ret = -1;
	}

	free(orig_wdir);
	return ret;
}
/*
 * Sending Files (Host -> BlueSCSI / shared)
 */
static int bluescsi_sendfile(int dev, char *path)
{
	char cmd[10] = { BLUESCSI_TOOLBOX_SEND_FILE_PREP, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	char filename[NAME_BUF_SIZE];
	char *base_name;
	char *send_buf;
	long int bytes_read = 0;
	long int actual_read = 0;
	long int blk_offset = 0; /* Offset in 512-byte blocks */
	int num_blocks;
	int ret;
	FILE *fd;
	long int filesize;
	struct stat st;

	if (verbose)
		fprintf(stdout, "sendfile: %s\n", path);

	/* Extract base filename */
	base_name = strrchr(path, '/');
	if (base_name == NULL) {
		base_name = path;
	} else {
		base_name++;
	}

	if (strlen(base_name) >= NAME_BUF_SIZE) {
		fprintf(stderr, "Error: sendfile Filename too long: %s\n", base_name);
		return -1;
	}

	memset(filename, 0, NAME_BUF_SIZE);
	strncpy(filename, base_name, NAME_BUF_SIZE - 1);

	/* Open file */
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

	/* 1. Send BLUESCSI_TOOLBOX_SEND_FILE_PREP (0xD3) */
	if (scsi_send_commandw(dev, (unsigned char *)cmd, SCSI_CMD_LENGTH, (unsigned char *)filename, 33) != 0) {
		fprintf(stderr, "Error: sendfileprep failed - %s\n", strerror(errno));
		fclose(fd);
		return 1;
	}

	send_buf = (char *)malloc(SEND_BUF_SIZE);
	if (send_buf == NULL) {
		fprintf(stderr, "Error: sendfile couldn't allocate send buffer\n");
		fclose(fd);
		return 1;
	}

	/* 2. Send Data Blocks via BLUESCSI_TOOLBOX_SEND_FILE_10 (0xD4) */
	while (bytes_read < filesize) {
		long int chunk = (filesize - bytes_read) < SEND_BUF_SIZE ? (filesize - bytes_read) : SEND_BUF_SIZE;
		memset(send_buf, 0, SEND_BUF_SIZE);

		actual_read = fread(send_buf, 1, chunk, fd);
		if (actual_read <= 0) {
			fprintf(stderr, "Error: fread failed or returned 0 at offset %ld\n", bytes_read);
			free(send_buf);
			fclose(fd);
			return 1;
		}

		memset(cmd, 0, sizeof(cmd));
		cmd[0] = BLUESCSI_TOOLBOX_SEND_FILE_10;

		/* CDB[3..5]: 24-bit big endian block offset (512-byte blocks) */
		cmd[3] = (unsigned char)((blk_offset >> 16) & 0xFF);
		cmd[4] = (unsigned char)((blk_offset >>  8) & 0xFF);
		cmd[5] = (unsigned char)((blk_offset      ) & 0xFF);

		if (actual_read % SEND_BLOCK_SIZE == 0) {
			/* Block Mode: Transfer size = CDB[6] * 512 bytes */
			num_blocks = actual_read / SEND_BLOCK_SIZE;
			cmd[1] = 0;
			cmd[2] = 0;
			cmd[6] = (unsigned char)(num_blocks & 0xFF);
		} else {
			/* Legacy Mode: CDB[6] = 0, CDB[1..2] = raw byte count */
			num_blocks = (actual_read + SEND_BLOCK_SIZE - 1) / SEND_BLOCK_SIZE;
			cmd[1] = (unsigned char)((actual_read >> 8) & 0xFF);
			cmd[2] = (unsigned char)(actual_read & 0xFF);
			cmd[6] = 0;
		}

		ret = scsi_send_commandw(dev, (unsigned char *)cmd, sizeof(cmd), (unsigned char *)send_buf, actual_read);
		if (ret != 0) {
			fprintf(stderr, "Error: sendfile10 failed at block %ld - %s\n", blk_offset, strerror(errno));
			free(send_buf);
			fclose(fd);
			return 1;
		}

		bytes_read += actual_read;
		blk_offset += num_blocks;
	}

	free(send_buf);

	/* 3. Send BLUESCSI_TOOLBOX_SEND_FILE_END (0xD5) */
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
 * Debug control
 */
static int bluescsi_getdebug (int dev)
{
	int ret;
	char cmd[10] = {BLUESCSI_TOOLBOX_TOGGLE_DEBUG, 0, 0, 0, 0, 0, 0, 0, 0, 0};	
	char buf[1];
	cmd[1] = DEBUG_GET;
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
	ret = buf[0]; 
	return ret;
}

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
	ret = buf[0];
	if (ret < 0 || ret > MAX_FILES)
	{
		fprintf (stderr,"Error: countcds invalid count %i\n", ret);
		return -1;
	}
	return ret;
}

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
		fprintf (stderr, "Error: CD number requested invalid: %i\n", num_cds);
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
		free(buf);
		return -1;
	}
	j = 0;
	for (i = 0; i < buf_size; i++)
	{
		if (j == 0)
			fprintf (stdout, "#%i ", (buf[i]));
		if (j >= 2 && j <= 34)
			fprintf (stdout, "%c", buf[i]);
		j++;
		if (j >= sizeof(ToolboxFileEntry))
		{
			j = 0;
			fprintf (stdout, "\n");
		}
	}
	fprintf (stdout, "\n");
	free(buf);
	return 1;
}

static int bluescsi_listfiles(int dev, int print)
{
	char cmd[10] = {BLUESCSI_TOOLBOX_MODE_FILES, 0, 0, 0, 0, 0, 0, 0, 0, 0};
	char *buf;
	int i;
	int buf_size;
	int num_files;
	
	if (verbose)
		fprintf (stdout, "Listing files on dev %d\n", dev);

	num_files = bluescsi_countfiles (dev);
	if (num_files < 0 || num_files > MAX_FILES)
	{
		fprintf (stderr, "Error: listfiles num_files invalid: %i\n", num_files);
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
		free(buf);
		return -1;
	}

	for (i = 0; i < num_files; i++) {
		memcpy(&files[i], buf + i * sizeof(ToolboxFileEntry), sizeof(ToolboxFileEntry));
		files[i].name[sizeof(files[i].name) - 1] = '\0';
	}
	free(buf);

	if (verbose || print)
	{	
		for (i = 0; i < num_files; i++)
			fprintf (stdout, "#%i %s %llu bytes\n", files[i].index, files[i].name, size_to_long(files[i].size));
	}
	return 0;
}

/*
 * Receiving Files (BlueSCSI /shared -> Host)
 * BLUESCSI_TOOLBOX_GET_FILE (0xD1)
 */
static int bluescsi_getfile(int dev, int idx, char *outdir)
{
	char cmd[10];
	char *buf;
	FILE *fd;
	char *filename;
	long int total_bytes;
	long int total_blocks;
	long int blk_offset = 0;
	long int bytes_written = 0;
	long int bytes_to_read;
	int blocks_to_req;
	int ret;

	if (strlen(outdir) < 1)
		strcpy(outdir, "./");

	if (bluescsi_listfiles(dev, 0) != 0)
	{
		fprintf(stderr, "Error: getfile couldn't listfiles\n");
		return -1;
	}

	if (idx < 0 || idx >= files_count)
	{
		fprintf(stderr, "Error: invalid file index %d\n", idx);
		return -1;
	}

	total_bytes = size_to_long(files[idx].size);
	if (verbose)
		fprintf(stdout, "getfile :#%i %s %li bytes\n", files[idx].index, files[idx].name, total_bytes);

	filename = (char *)malloc(strlen(outdir) + strlen(files[idx].name) + 2);
	if (filename == NULL)
	{
		fprintf(stderr, "Error: malloc failed for filename\n");
		return -1;
	}

	if (outdir[strlen(outdir) - 1] == '/')
		sprintf(filename, "%s%s", outdir, files[idx].name);
	else
		sprintf(filename, "%s/%s", outdir, files[idx].name);

	fprintf(stdout, "Fetching %s (%ld bytes)\n", files[idx].name, total_bytes);
	fd = fopen(filename, "wb");
	if (fd == NULL)
	{
		fprintf(stderr, "Error: getfile couldn't open %s\n", filename);
		free(filename);
		return -1;
	}

	if (total_bytes == 0)
	{
		fclose(fd);
		free(filename);
		return 0;
	}

	buf = (char *)malloc(GET_BUF_SIZE);
	if (buf == NULL)
	{
		fprintf(stderr, "Error: malloc failed for receive buffer\n");
		fclose(fd);
		free(filename);
		return -1;
	}

	/* Total 4096-byte blocks */
	total_blocks = (total_bytes + GET_BLOCK_SIZE - 1) / GET_BLOCK_SIZE;

	while (blk_offset < total_blocks)
	{
		long int blocks_remaining = total_blocks - blk_offset;
		blocks_to_req = (blocks_remaining < GET_BLOCKS_PER_XFER) ? blocks_remaining : GET_BLOCKS_PER_XFER;

		/* Determine exact bytes to expect for this SCSI command */
		if (blk_offset + blocks_to_req == total_blocks) {
			/* Final chunk includes last block, size buffer to remaining file bytes */
			bytes_to_read = total_bytes - bytes_written;
		} else {
			bytes_to_read = blocks_to_req * GET_BLOCK_SIZE;
		}

		memset(cmd, 0, sizeof(cmd));
		cmd[0] = BLUESCSI_TOOLBOX_GET_FILE;
		cmd[1] = idx;
		cmd[2] = (unsigned char)((blk_offset >> 24) & 0xFF);
		cmd[3] = (unsigned char)((blk_offset >> 16) & 0xFF);
		cmd[4] = (unsigned char)((blk_offset >>  8) & 0xFF);
		cmd[5] = (unsigned char)((blk_offset      ) & 0xFF);
		cmd[6] = (unsigned char)(blocks_to_req & 0xFF);

		memset(buf, 0, GET_BUF_SIZE);
		ret = scsi_send_command(dev, (unsigned char *)cmd, sizeof(cmd), (unsigned char *)buf, bytes_to_read);
		if (ret != 0)
		{
			fprintf(stderr, "Error: getfile failed during transfer at block %ld - %s\n", blk_offset, strerror(errno));
			fclose(fd);
			free(buf);
			free(filename);
			return -1;
		}

		if (fwrite(buf, 1, bytes_to_read, fd) != (size_t)bytes_to_read)
		{
			fprintf(stderr, "Error: fwrite failed writing to %s\n", filename);
			fclose(fd);
			free(buf);
			free(filename);
			return -1;
		}

		bytes_written += bytes_to_read;
		blk_offset += blocks_to_req;
	}

	fclose(fd);
	free(buf);
	free(filename);
	return 0;
}

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

static int bluescsi_check_vendor_page(int dev)
{
	static const unsigned char BlueSCSIVendorPage[] = {
		0x31, /* Page code */
		42,   /* Page length */
		'B','l','u','e','S','C','S','I',' ','i','s',' ','t','h','e',' ','B','E','S','T',' ',
		'S','T','O','L','E','N',' ','F','R','O','M',' ','B','L','U','E','S','C','S','I',0x00
	};

	unsigned char cmd[6];
	unsigned char buf[64];
	unsigned char bdl;
	unsigned char returned_page_code;
	int page_offset;

	/* MODE SENSE (6) CDB: Opcode 0x1A, requesting Page 0x31 */
	cmd[0] = 0x1A; /* MODE SENSE (6) */
	cmd[1] = 0x08; /* DBD = 1 (Disable Block Descriptors) */
	cmd[2] = 0x31; /* Page code 0x31 */
	cmd[3] = 0x00; /* Subpage code */
	cmd[4] = 64;   /* Allocation length */
	cmd[5] = 0x00; /* Control */

	memset(buf, 0, sizeof(buf));
	
	if (verbose)
		fprintf(stdout, "Fetching BlueSCSI vendor page\n");
	if (scsi_send_command(dev, cmd, sizeof(cmd), buf, sizeof(buf)) != 0)
	{
		if (verbose)
			fprintf(stderr, "Error: MODE SENSE (6) command failed - %s\n", strerror(errno));
		return 1;
	}

	/* MODE SENSE (6) header is 4 bytes: [0]=length, [1]=medium type, [2]=dev param, [3]=BDL */
	bdl = buf[3];
	page_offset = 4 + bdl;

	/* Ensure response length contains the full expected page */
	if (page_offset + (int)sizeof(BlueSCSIVendorPage) > (int)sizeof(buf))
	{
		if (verbose)
			fprintf(stderr, "Error: MODE SENSE response too short for vendor page 0x31\n");
		return 1;
	}

	/* Mask out bit 7 (PS - Parameters Savable bit) on returned page code byte */
	returned_page_code = buf[page_offset] & 0x3F;

	if (returned_page_code != BlueSCSIVendorPage[0] ||
	    memcmp(&buf[page_offset + 1], &BlueSCSIVendorPage[1], sizeof(BlueSCSIVendorPage) - 1) != 0)
	{
		if (verbose)
			fprintf(stderr, "Error: Vendor page 0x31 data mismatch\n");
		return 1;
	}
	
	if (verbose)
		fprintf(stdout, "Vendor page: %.*s\n", 41, (char *)&buf[page_offset + 2]);
	return 0;
}

static int bluescsi_inquiry(int dev, int print)
{
	char cmd[] ={SCSI_INQUIRY, 0, 0, 0, sizeof(scsi_inquiry), 0};	
	char buf[sizeof(scsi_inquiry)];
	const char *BlueSCSI_vendor_id = "BLUESCSI";
	scsi_inquiry inq;
	int i;
	char* dev_flags;
	int additional_len;
	int total_len;
	int toolbox_api_version;
	unsigned char dev_map[8];
	unsigned char api_ver;
	unsigned char caps;

	memset(buf, 0, sizeof(buf));
	if (verbose)
		fprintf(stdout, "Sending SCSI Inquiry command\n");
	if (scsi_send_command(dev, (unsigned char *)cmd, sizeof(cmd), (unsigned char *)buf, sizeof(buf)) != 0)
	{
		fprintf (stderr, "Error: inquiry command failed - %s\n", strerror(errno));
		return 1;
	}

	memset (&inq, 0, sizeof(scsi_inquiry));
	memcpy (&inq.version, &buf[2], 1);
	memcpy (&inq.vendor_id, &buf[8], sizeof(inq.vendor_id) - 1);
	inq.vendor_id[8] = '\0';
	memcpy (&inq.product_id, &buf[16], sizeof(inq.product_id) - 1);
	inq.product_id[16] = '\0';
	memcpy (&inq.product_rev, &buf[32], sizeof(inq.product_rev) - 1);
	inq.product_rev[4] = '\0';
	
	if (verbose || print)
	{
		fprintf (stdout, "SCSI version: %i\n", inq.version);
		fprintf (stdout, "vendor_id: %s \nproduct_id: %s\n", inq.vendor_id, inq.product_id);
		fprintf (stdout, "product_rev: %s\n", inq.product_rev);
	}
	
	/* Do not proceed if it's not a BlueSCSI device */
	if (strstr (inq.vendor_id, BlueSCSI_vendor_id) == NULL)
	{
		fprintf (stderr, "Error: didn't find \"%s\" in vendor_id: %s\n", BlueSCSI_vendor_id, inq.vendor_id);
		return 1;
	}
	else if (verbose || print)
		fprintf (stdout, "debug mode: %i\n", bluescsi_getdebug(dev)); /* Don't try to get debug mode if it isn't a BlueSCSI */

	/* Check the BlueSCSIVendorPage */
	if (bluescsi_check_vendor_page(dev) != 0)
	{
		fprintf (stderr, "Error: didn't find BlueSCSI vendor page\n");
		return 1;
	}

	additional_len = buf[4];
	total_len = additional_len + 5;

	if (total_len <= sizeof(buf)) {
		toolbox_api_version = buf[total_len - 1];
		if (verbose)
			fprintf(stdout, "Toolbox API version: %u\n", toolbox_api_version);

		if (toolbox_api_version < BLUESCSI_TOOLBOX_API_VER) {
			fprintf(stdout, "WARNING! Toolbox API version %u too old, expecting: %u\n", toolbox_api_version, BLUESCSI_TOOLBOX_API_VER);
		}
	} else {
		fprintf(stdout, "Toolbox API version: not available (length mismatch)\n");
		return 1;
	}

	/* Use Metadata subcommand 0x00 to list devices */
	if (bluescsi_metadata_list_devices(dev, dev_map) == 0) {
		if (verbose)
			fprintf (stdout, "Device flags (Metadata 0xD9:00): ");
		for (i = 0; i < 8; i++)
		{
			device_list[i] = dev_map[i];
			if (verbose)
				fprintf (stdout,"%02x ", dev_map[i]);
		}
		if (verbose)
			fprintf(stdout, "\n");
	}
	else if (bluescsi_listdevices(dev, &dev_flags) == 0) {
		/* Fallback to legacy device list command if metadata 0xD9:00 fails */
		if (verbose)
			fprintf (stdout, "Device flags (Legacy): ");
		for (i = 0; i < 8; i++)
		{
			device_list[i] = dev_flags[i];
			if (verbose)
				fprintf (stdout,"%02x ", (unsigned char) dev_flags[i]);
		}
		if (verbose)
			fprintf(stdout, "\n");
		free(dev_flags);
	}
	else {
		fprintf (stderr, "Failed to fetch device flags: %s\n", strerror(errno));
		return 1;
	}

	/* Fetch Capabilities via Metadata 0xD9:01 */
	bluescsi_metadata_get_capabilities(dev, &api_ver, &caps);

	return 0;
}

static void do_drive(char *path, int mode, int verbose, int cd_img, int file, char *outdir)
{
	int dev;
	int dev_scsi_id;
	int readonly = 0;
	
	if (mode == MODE_CD || cd_img != NOT_ACTIVE)
		readonly = 1;

	dev = scsi_open(path, readonly);
	if (dev < 0) {
		if (!readonly)
		{
			fprintf (stderr, "Error opening device for read/write, trying to open readonly\n");
			dev = scsi_open(path, 1);
		}
		if (dev < 0) {
			fprintf(stderr, "ERROR: Cannot open device: %s\nTry running again as root\n", strerror(errno));
			exit(1);
		}
	}

	if (bluescsi_inquiry (dev, PRINT_OFF) != 0)
	{
		fprintf (stderr, "Didn't find a BlueSCSI device at %s\n", path);
		scsi_close (dev);
		exit(1);
	}
	
	if ((dev_scsi_id = path_to_devnum(path)) < 0)
	{
		fprintf (stderr, "Failed to get dev_scsi_id from path_to_devnum\n");
		scsi_close(dev);
		exit(1);
	}

	if (mode == MODE_CD)
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
	else if (mode == MODE_INQUIRY)
		bluescsi_inquiry(dev, PRINT_ON);
	else if (mode == MODE_DEBUG)
		bluescsi_setdebug(dev, file);
	else if (mode == MODE_SHARED)
		bluescsi_listfiles(dev, PRINT_ON);
	else if (mode == MODE_PUT)
		bluescsi_sendfile (dev, outdir);
	else if (mode == MODE_GET_WDIR)
		bluescsi_print_wdir(dev);
	else if (mode == MODE_SET_WDIR)
		bluescsi_set_wdir(dev, outdir);
	else if (mode == MODE_GET_LOG)
		bluescsi_get_log(dev);
	else if (file != NOT_ACTIVE)
		bluescsi_getfile (dev, file, outdir);
	else if (cd_img != NOT_ACTIVE)
	{
		if (device_list[dev_scsi_id] != TYPE_CD)
			fprintf (stderr, "Device doesn't seem to be a CD drive? Detected type %i on SCSI ID %i\n", device_list[dev_scsi_id], dev_scsi_id);
		else
			bluescsi_setnextcd(dev, cd_img);
	}
	
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
	fprintf(stderr, "\t-w      : get current working directory\n");
	fprintf(stderr, "\t-W dir  : set working directory\n");
	fprintf(stderr, "\t-L      : Show BlueSCSI log\n");
	fprintf(stderr, "\t-d num  : set debug mode (0 = off, 1 - on)\n");
	fprintf(stderr, "\n\nPlease make sure you run the program as root.\n");
}

int main(int argc, char *argv[])
{
	int c, cdimg = NOT_ACTIVE, mode = 0, file = NOT_ACTIVE;
	char outdir[1024];

	memset(outdir, 0, sizeof(outdir));

	while ((c = getopt(argc, argv, "hvlsic:d:g:o:p:wW:L")) != -1) switch (c) {
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
			mode = MODE_PUT;
			break;
		case 'l':
			mode = MODE_CD;
			break;
		case 's':
			mode = MODE_SHARED;
			break;
		case 'i':
			mode = MODE_INQUIRY;
			break;
		case 'd':
			mode = MODE_DEBUG;
			file = atoi(optarg);
			break;
		case 'w':
			mode = MODE_GET_WDIR;
			break;
		case 'W':
			mode = MODE_SET_WDIR;
			strncpy(outdir, optarg, sizeof(outdir) - 1);
			break;
		case 'L':
			mode = MODE_GET_LOG;
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
		mediad_stop ();

	if (argc < 1) {
		fprintf (stderr, "No device path entered\n");
		usage();
		return 1;
	} else if (argc > 1) {
		fprintf(stderr, "WARNING: Options after '%s' ignored.\n", argv[0]);
	}

	do_drive(argv[0], mode, verbose, cdimg, file, outdir);
	
	if (cdimg != -1)
		mediad_start ();
	
	return 0;
}

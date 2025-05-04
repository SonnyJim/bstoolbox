/*
 * This file contains the code for talking to SCSI devices on Linux
 */

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <scsi/sg.h>
#include <string.h>
#include <errno.h>

#include "os.h"

extern int verbose;
//Run when a CD is changed
//TODO Is there a systemd way of doing this?
int mediad_start(void) 
{
	fprintf(stderr, "mediad_start(): Not implemented on Linux\n");
    	return 1;
}

int mediad_stop(void) 
{
	fprintf(stderr, "mediad_stop(): Not implemented on Linux\n");
    	return 1;
}

int scsi_open(char *path, int readonly)
{
	if (readonly)
		return open(path, O_RDONLY | O_SYNC);
	
	else
		return open(path, O_RDWR | O_SYNC);
}

int scsi_close(int dev)
{
	return close(dev);
}

int scsi_send_command(int dev, unsigned char *cmd, int cmd_len, unsigned char *buf, int buf_len)
{
	int i;
	struct sg_io_hdr io_hdr = { 0 };

	io_hdr.interface_id = 'S';
	io_hdr.cmdp = cmd;
	io_hdr.cmd_len = cmd_len;
	io_hdr.mx_sb_len = 0;
	io_hdr.dxfer_direction = SG_DXFER_FROM_DEV;
	io_hdr.dxferp = buf;
	io_hdr.dxfer_len = buf_len;
	/* 5000ms should be enough */
	io_hdr.timeout = 5000;

	if (verbose){
		fprintf(stdout, "Sending SCSI command: ");
		for (i = 0; i < cmd_len; ++i) {
			fprintf(stdout, "%02x ", (unsigned char) io_hdr.cmdp[i]);
		}
		fprintf(stdout, "\n");
	}

	if (ioctl(dev, SG_IO, &io_hdr) < 0)
		return 1;

	return 0;
}

//TODO Document this or just do it better
int scsi_send_commandw(int dev, unsigned char *cmd, int cmd_len, unsigned char *buf, int buf_len)
{
	int i;
	struct sg_io_hdr io_hdr = { 0 };

	io_hdr.interface_id = 'S';
	io_hdr.cmdp = cmd;
	io_hdr.cmd_len = cmd_len;
	io_hdr.mx_sb_len = 0;
	io_hdr.dxfer_direction = SG_DXFER_TO_DEV;
	io_hdr.dxferp = buf;
	io_hdr.dxfer_len = buf_len;
	/* 5000ms should be enough */
	io_hdr.timeout = 5000;

	if (verbose){
		fprintf(stdout, "Sending SCSI command: ");
		for (i = 0; i < cmd_len; ++i) {
			fprintf(stdout, "%02x ", (unsigned char) io_hdr.cmdp[i]);
		}
		fprintf(stdout, "\n");
		fprintf(stdout, "Data to device (%d bytes):\n", buf_len);
		for (i = 0; i < buf_len; i += 16) {
			int j;
			fprintf(stdout, "  %04x: ", i);

			// Hex part
			for (j = 0; j < 16; ++j) {
			    if (i + j < buf_len) {
				fprintf(stdout, "%02x ", buf[i + j]);
			    } else {
				fprintf(stdout, "   "); // padding
			    }
			}

			fprintf(stdout, " |");

			// ASCII part
			for (j = 0; j < 16; ++j) {
			    if (i + j < buf_len) {
				unsigned char c = buf[i + j];
				fprintf(stdout, "%c", (c >= 32 && c <= 126) ? c : '.');
			    }
			}

			fprintf(stdout, "|\n");
		}
	}
	if (ioctl(dev, SG_IO, &io_hdr) < 0)
		return 1;

	return 0;
}

/**
 * Extract the SCSI ID from a /dev/sgX device path.
 * Returns: SCSI ID on success, -1 on failure.
 */
int path_to_devnum(const char *path) {
    struct sg_scsi_id scsi_id;
    int fd;

    // Validate the path format
    if (strncmp(path, "/dev/sg", 7) != 0 || strlen(path) < 8) {
        fprintf(stderr, "ERROR: Unsupported device path format: %s\n", path);
        return -1;
    }

    // Open the device
    fd = open(path, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "ERROR: Failed to open %s: %s\n", path, strerror(errno));
        return -1;
    }

    // Attempt to get the SCSI ID
    if (ioctl(fd, SG_GET_SCSI_ID, &scsi_id) < 0) {
        fprintf(stderr, "ERROR: ioctl SG_GET_SCSI_ID failed on %s: %s\n", path, strerror(errno));
        close(fd);
        return -1;
    }

    close(fd);
    if (verbose)
	    fprintf(stderr, "Found something at SCSI ID%i\n", scsi_id.scsi_id);
    return scsi_id.scsi_id;
}

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <scsi/sg.h>
#include <string.h>

#include "os.h"

extern int verbose;
int mediad_start(void) {
	fprintf(stderr, "mediad_start(): Not implemented on Linux\n");
    	return 1;
}

int mediad_stop(void) {
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

#if 0
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
#endif

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
	}
	
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
	if (ioctl(dev, SG_IO, &io_hdr) < 0)
		return 1;

	return 0;
}

// Extract the numeric part of /dev/sgN or /dev/srN
int path_to_devnum(const char *path) {
    int dev_path_num;

    // Match /dev/sg0, /dev/sg1, etc.
    if (sscanf(path, "/dev/sg%d", &dev_path_num) == 1) {
        return dev_path_num;
    }
    // Linux pattern: /dev/sdX where X is A-Z
    else if (strncmp(path, "/dev/sd", 7) == 0 && strlen(path) == 8) {
        char drive_letter = path[7];
        if (drive_letter >= 'a' && drive_letter <= 'z') {
            return drive_letter - 'a';
        } else if (drive_letter >= 'A' && drive_letter <= 'Z') {
            return drive_letter - 'A';
        } else {
            fprintf(stderr, "ERROR: Invalid device letter: %c\n", drive_letter);
            return -1;
        }
    } 
    // Match /dev/sr0, /dev/sr1 (SCSI CD-ROMs)
    else if (sscanf(path, "/dev/sr%d", &dev_path_num) == 1) {
        return dev_path_num;
    }

    fprintf(stderr, "ERROR: Invalid or unsupported Linux device path format: %s\n", path);
    return -1;
}

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/dsreq.h>
#include <invent.h>
#include <errno.h>

#include "os.h"

extern int verbose;

int scsi_open(char *path)
{
	return open(path, O_RDWR | O_SYNC);
}


int scsi_close(int dev)
{
	return close(dev);
}

int scsi_send_command(int dev, unsigned char *cmd, int cmd_len, unsigned char *buf, int buf_len)
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
	//r.ids_sensebuf = (caddr_t) buf;
	//r.ds_senselen = buf_len;
	r.ds_sensebuf = NULL;
	r.ds_senselen = 0;
	
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

//TODO Document this or just do it better
int scsi_send_commandw(int dev, unsigned char *cmd, int cmd_len, unsigned char *buf, int buf_len)
{
	int i;
	dsreq_t r;
	memset(&r, 0, sizeof(dsreq_t));
	
	/* Assemble the request structure */
	r.ds_cmdbuf   = (caddr_t) cmd;
	r.ds_cmdlen   = cmd_len;
	r.ds_databuf  = (caddr_t) buf;
	r.ds_datalen  = buf_len;
	//r.ids_sensebuf = (caddr_t) buf;
	//r.ds_senselen = buf_len;
	r.ds_sensebuf = NULL;
	r.ds_senselen = 0;
	
	r.ds_time     = 5 * 1000;  /* 5 seconds should be enough */
	r.ds_flags    = DSRQ_WRITE;
	
	if (verbose){
		fprintf(stdout, "Sending SCSI command: ");
		for (i = 0; i < cmd_len; ++i) {
			fprintf(stdout, "%02x ", (unsigned char)r.ds_cmdbuf[i]);
		}
		fprintf(stdout, "\n");
	}	
	/* Issue the request */
	if (ioctl(dev, DS_ENTER, &r))
		return -errno;
	return 0;
}

// Extract the device number from the path 
// TODO This will probably break very easily
int path_to_devnum(const char *path) {
	int dev_path_num;

	if (sscanf(path, "/dev/scsi/sc%*dd%dl%*d", &dev_path_num) != 1) {
		fprintf(stderr, "ERROR: Invalid path format: %s\n", path);
		return -1;
	}

	return dev_path_num;
}

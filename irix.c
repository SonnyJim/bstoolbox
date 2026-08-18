#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/dsreq.h>
#include <invent.h>
#include <sys/invent.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "os.h"

#ifndef INV_PERIPH
#define INV_PERIPH 9
#endif

#define MAX_READY_RETRIES 10
#define SENSE_BUF_LEN     64
#define STATUS_CHECKCOND  0x02

extern int verbose;

int mediad_start(void) {
    int status;
    if (verbose)
        fprintf(stdout, "Starting mediad...\n");
    status = system("/etc/init.d/mediad start");
    if (status != 0) {
        fprintf(stderr, "Failed to start mediad service: %s\n", strerror(errno));
        return 1;
    }
    return 0;
}

int mediad_stop(void) {
    int status;
    if (verbose)
        fprintf(stdout, "Stopping mediad...\n");
    status = system("/etc/init.d/mediad stop");
    if (status != 0) {
        fprintf(stderr, "Failed to stop mediad service: %s\n", strerror(errno));
        return 1;
    }
    return 0;
}

static int test_dsreq_flags(int dev_fd, uint flag)
{
   dsconf_t config;
   int ret;
   ret = ioctl(dev_fd, DS_CONF, &config);
   if (verbose) {
       fprintf(stdout, "dsc_iomax: %i\n", config.dsc_iomax);
       fprintf(stdout, "dsc_biomax: %i\n", config.dsc_biomax);
       fprintf(stdout, "SCSI Bus:%i Max Target:%i Max LUN:%i\n", config.dsc_bus, config.dsc_imax, config.dsc_lmax);
   }
   if (!ret) {
      return (flag & config.dsc_flags);
   } else {
      return 0;
   }
}

int scsi_open(char *path, int readonly)
{
    int ret;
    if (readonly)
        ret = open(path, O_RDONLY | O_SYNC);
    else
        ret = open(path, O_RDWR | O_SYNC);
    if (ret >= 0 && verbose)
        test_dsreq_flags(ret, DSRQ_BUF);
    return ret;
}

int scsi_close(int dev)
{
    return close(dev);
}

#define MAX_READY_RETRIES 10
#define SENSE_BUF_LEN 64
#define STATUS_CHECKCOND 0x02

/*
 * Execute a SCSI command with the specified data direction.
 *
 * direction should be:
 *     DSRQ_READ   Device -> host
 *     DSRQ_WRITE  Host -> device
 *
 * Returns:
 *      0       success
 *     -errno   ioctl failure
 *     -EIO     SCSI CHECK CONDITION / other SCSI error
 */
static int scsi_send_command_dir(int dev,
                                 unsigned char *cmd,
                                 int cmd_len,
                                 unsigned char *buf,
                                 int buf_len,
                                 int direction)
{
    int i;
    int err;
    dsreq_t r;
    unsigned char sense_data[SENSE_BUF_LEN];

    if (!cmd || cmd_len <= 0)
        return -EINVAL;

    if (buf_len < 0)
        return -EINVAL;

    if (buf_len > 0 && !buf)
        return -EINVAL;

    memset(&r, 0, sizeof(dsreq_t));
    memset(sense_data, 0, sizeof(sense_data));

    r.ds_cmdbuf   = (caddr_t)cmd;
    r.ds_cmdlen   = cmd_len;
    r.ds_databuf  = (caddr_t)buf;
    r.ds_datalen  = buf_len;

    /*
     * ds_senselen is a u_char on IRIX, so do not use a
     * 256-byte sense buffer here: (u_char)256 == 0.
     */
    r.ds_sensebuf = (caddr_t)sense_data;
    r.ds_senselen = sizeof(sense_data);

    r.ds_time = 30 * 1000;

    /*
     * Always request sense information.  Add the requested
     * data direction only when there is actually data.
     */
    r.ds_flags = DSRQ_SENSE;

    if (buf != NULL && buf_len > 0)
        r.ds_flags |= direction;

    if (verbose) {
        fprintf(stdout, "Sending SCSI %s command: ",
                direction == DSRQ_WRITE ? "write" : "read");

        for (i = 0; i < cmd_len; ++i)
            fprintf(stdout, "%02x ", (unsigned char)cmd[i]);

        fprintf(stdout, "\n");
    }

    if (ioctl(dev, DS_ENTER, &r) != 0) {
        err = errno;

        if (verbose)
            fprintf(stderr, "DS_ENTER failed: %s\n", strerror(err));

        return -err;
    }

    if (r.ds_status == STATUS_CHECKCOND) {
        fprintf(stderr, "SCSI CHECK CONDITION\n");

        /*
         * Fixed format sense data has ASC/ASCQ at bytes 12/13.
         */
        if (r.ds_senselen >= 14) {
            unsigned char key  = sense_data[2] & 0x0F;
            unsigned char asc  = sense_data[12];
            unsigned char ascq = sense_data[13];

            fprintf(stderr,
                    "Sense Key: 0x%02x, ASC: 0x%02x, ASCQ: 0x%02x\n",
                    key, asc, ascq);
        }

        return -EIO;
    }

    if (r.ds_status != 0) {
        fprintf(stderr,
                "SCSI command failed, status: 0x%02x\n",
                r.ds_status);

        return -EIO;
    }

    return 0;
}


/*
 * Wait until the SCSI device reports that it is ready.
 */
static int scsi_wait_until_ready(int dev)
{
    dsreq_t tur;
    unsigned char tur_cmd[6] = {
        0x00, 0, 0, 0, 0, 0
    };
    unsigned char sense_data[SENSE_BUF_LEN];
    int i;

    for (i = 0; i < MAX_READY_RETRIES; ++i) {
        memset(&tur, 0, sizeof(dsreq_t));
        memset(sense_data, 0, sizeof(sense_data));

        tur.ds_cmdbuf   = (caddr_t)tur_cmd;
        tur.ds_cmdlen   = sizeof(tur_cmd);
        tur.ds_databuf  = NULL;
        tur.ds_datalen  = 0;
        tur.ds_sensebuf = (caddr_t)sense_data;
        tur.ds_senselen = sizeof(sense_data);
        tur.ds_time     = 1000;
        tur.ds_flags    = DSRQ_SENSE;

        if (ioctl(dev, DS_ENTER, &tur) == 0) {
            if (tur.ds_status == 0)
                return 0;

            if (tur.ds_status == STATUS_CHECKCOND) {
                if (verbose) {
                    fprintf(stderr,
                            "SCSI CHECK CONDITION on TUR, "
                            "sense key: 0x%02x\n",
                            sense_data[2] & 0x0F);
                }
            } else if (verbose) {
                fprintf(stderr,
                        "TEST UNIT READY returned status: 0x%02x\n",
                        tur.ds_status);
            }
        } else {
            if (verbose)
                perror("TEST UNIT READY ioctl failed");
        }

        usleep(100000);
    }

    return -ETIMEDOUT;
}

/*
 * Send a SCSI command which reads data from the device.
 */
int scsi_send_command(int dev,
                      unsigned char *cmd,
                      int cmd_len,
                      unsigned char *buf,
                      int buf_len)
{
    return scsi_send_command_dir(dev,
                                 cmd,
                                 cmd_len,
                                 buf,
                                 buf_len,
                                 DSRQ_READ);
}


/*
 * Send a SCSI command which writes data to the device.
 *
 * This retains the existing TEST UNIT READY check before
 * performing the write.
 */
int scsi_send_commandw(int dev,
                       unsigned char *cmd,
                       int cmd_len,
                       unsigned char *buf,
                       int buf_len)
{
    int ret;

    ret = scsi_wait_until_ready(dev);

    if (ret != 0) {
        fprintf(stderr,
                "Device not ready, aborting transfer\n");
        return ret;
    }

    return scsi_send_command_dir(dev,
                                 cmd,
                                 cmd_len,
                                 buf,
                                 buf_len,
                                 DSRQ_WRITE);
}
int path_to_devnum(const char *path) {
    int dev_path_num;

    if (sscanf(path, "/dev/scsi/sc%*dd%dl%*d", &dev_path_num) != 1) {
        fprintf(stderr, "ERROR: Invalid path format: %s\n", path);
        return -1;
    }

    return dev_path_num;
}

/* Helper to get scsi path from network name, eg 'dp0' -> '/dev/scsi/sc0dd010 */
int get_scsi_path_for_iface(const char *ifname, char *out_path, size_t path_len) {
    inventory_t *inv;
    int dp_count = 0;
    char inv_name[32];

    if (!ifname || !*ifname) return -1;

    setinvent();

    while ((inv = getinvent()) != NULL) {
        /* Filter for DaynaPORT SCSI Peripheral entries (Class 9) */
        if (inv->inv_class == INV_PERIPH) {

            /* Build the string name for this inventory item */
            snprintf(inv_name, sizeof(inv_name), "dp%d", dp_count);

            /* Compare user's exact string against the inventory item's string */
            if (strcmp(ifname, inv_name) == 0) {
                snprintf(out_path, path_len, "/dev/scsi/sc%dd%dl0",
                         inv->inv_controller, inv->inv_unit);
                endinvent();
                return 0; /* Exact match in inventory */
            }

            dp_count++;
        }
    }

    endinvent();
    return -1; /* String does not exist in the inventory */
}

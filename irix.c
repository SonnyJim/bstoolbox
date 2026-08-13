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

int scsi_send_command(int dev, unsigned char *cmd, int cmd_len, unsigned char *buf, int buf_len)
{
    int i;
    int try;
    dsreq_t r;
    memset(&r, 0, sizeof(dsreq_t));
    
    r.ds_cmdbuf   = (caddr_t) cmd;
    r.ds_cmdlen   = cmd_len;
    r.ds_databuf  = (caddr_t) buf;
    r.ds_datalen  = buf_len;
    r.ds_sensebuf = NULL;
    r.ds_senselen = 0;
    r.ds_time     = 5 * 1000;
    
    /* Only set DSRQ_READ if receiving actual payload data */
    if (buf != NULL && buf_len > 0) {
        r.ds_flags = DSRQ_READ;
    } else {
        r.ds_flags = 0;
    }
    
    if (verbose){
        fprintf(stdout, "Sending SCSI command: ");
        for (i = 0; i < cmd_len; ++i) {
            fprintf(stdout, "%02x ", (unsigned char)r.ds_cmdbuf[i]);
        }
        fprintf(stdout, "\n");
    }	

    for (try = 0; try < 10; try++){
        if (ioctl(dev, DS_ENTER, &r) < 0 || r.ds_status != 0){
            fprintf(stderr, "WARNING: SCSI command failed/timed out (%d); retrying...\n", r.ds_status);
            sleep(try + 1);
        }
        else
            break;
        if (try >= 9){
            fprintf(stderr, "ERROR: Unable to send SCSI command (%d)\n", r.ds_status);
            return 1;
        }
    }
    return 0;
}

#define MAX_READY_RETRIES 10
#define SENSE_BUF_LEN 64
#define STATUS_CHECKCOND 0x02

static int scsi_wait_until_ready(int dev)
{
    dsreq_t tur;
    unsigned char tur_cmd[6] = { 0x00, 0, 0, 0, 0, 0 };  // TEST UNIT READY
    unsigned char sense_data[SENSE_BUF_LEN];
    int i;

    for (i = 0; i < MAX_READY_RETRIES; ++i) {
        memset(&tur, 0, sizeof(dsreq_t));
        memset(sense_data, 0, sizeof(sense_data));

        tur.ds_cmdbuf = (caddr_t) tur_cmd;
        tur.ds_cmdlen = sizeof(tur_cmd);
        tur.ds_databuf = NULL;
        tur.ds_datalen = 0;
        tur.ds_sensebuf = (caddr_t) sense_data;
        tur.ds_senselen = sizeof(sense_data);
        tur.ds_time = 1000;
        tur.ds_flags = DSRQ_SENSE; /* Pure sense flag, no READ/WRITE for 0-byte TUR */

        if (ioctl(dev, DS_ENTER, &tur) == 0) {
            if (tur.ds_status == 0) {
                return 0;  // Device ready
            } else if (tur.ds_status == STATUS_CHECKCOND) {
                if (verbose) {
                    fprintf(stderr, "SCSI CHECK CONDITION on TUR, sense key: 0x%02x\n", sense_data[2] & 0x0F);
                }
            }
        } else {
            if (verbose) {
                perror("TEST UNIT READY ioctl failed");
            }
        }

        usleep(100000); // 100ms delay
    }

    return -1;
}

int scsi_send_commandw(int dev, unsigned char *cmd, int cmd_len, unsigned char *buf, int buf_len)
{
    int i;
    dsreq_t r;
    unsigned char sense_data[256];

    if (scsi_wait_until_ready(dev) != 0) {
        fprintf(stderr, "Device not ready, aborting transfer\n");
        return -1;
    }

    memset(&r, 0, sizeof(dsreq_t));
    memset(sense_data, 0, sizeof(sense_data));

    r.ds_cmdbuf   = (caddr_t) cmd;
    r.ds_cmdlen   = cmd_len;
    r.ds_databuf  = (caddr_t) buf;
    r.ds_datalen  = buf_len;
    r.ds_sensebuf = (caddr_t) sense_data;
    r.ds_senselen = (u_char) sizeof(sense_data);
    r.ds_time     = 30 * 1000;
    
    if (buf != NULL && buf_len > 0) {
        r.ds_flags = DSRQ_WRITE | DSRQ_SENSE;
    } else {
        r.ds_flags = DSRQ_SENSE;
    }

    if (verbose) {
        fprintf(stdout, "Sending SCSI write command: ");
        for (i = 0; i < cmd_len; ++i)
            fprintf(stdout, "%02x ", (unsigned char)r.ds_cmdbuf[i]);
        fprintf(stdout, "\n");
    }

    if (ioctl(dev, DS_ENTER, &r) != 0) {
        perror("ioctl failed");
        return -errno;
    }

    if (r.ds_status == STATUS_CHECKCOND) {
        fprintf(stderr, "SCSI CHECK CONDITION\n");
        if (r.ds_senselen >= 14) {
            unsigned char key = sense_data[2] & 0x0F;
            unsigned char asc = sense_data[12];
            unsigned char ascq = sense_data[13];
            fprintf(stderr, "Sense Key: 0x%02x, ASC: 0x%02x, ASCQ: 0x%02x\n", key, asc, ascq);
        }
        return -EIO;
    }

    return 0;
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

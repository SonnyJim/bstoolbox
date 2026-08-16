/*
 * bswifi - BlueSCSI Wi-Fi control utility
 *
 * IRIX port of the BlueSCSI Wi-Fi SCSI protocol.
 *
 * Based on the working Macintosh implementation:
 *
 *   CDB[0] = BLUESCSI_NETWORK_WIFI_CMD
 *   CDB[1] = Wi-Fi subcommand
 *   CDB[3] = transfer length MSB
 *   CDB[4] = transfer length LSB
 *
 * The Macintosh implementation uses a 6-byte CDB.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "os.h"

#define BLUESCSI_NETWORK_WIFI_CMD             0x1C

#define BLUESCSI_NETWORK_WIFI_CMD_SCAN        0x01
#define BLUESCSI_NETWORK_WIFI_CMD_COMPLETE    0x02
#define BLUESCSI_NETWORK_WIFI_CMD_SCAN_RESULTS 0x03
#define BLUESCSI_NETWORK_WIFI_CMD_INFO        0x04
#define BLUESCSI_NETWORK_WIFI_CMD_JOIN        0x05

#define SCSI_CDB_LENGTH                       6
#define WIFI_NETWORK_ENTRY_COUNT              10

#define WIFI_NETWORK_FLAG_AUTH                (1 << 0)
#define WIFI_NETWORK_FLAG_HIDDEN              (1 << 7)
#define WIFI_NETWORK_ENTRY_SIZE               sizeof(struct wifi_network_entry)
#define WIFI_JOIN_REQUEST_SIZE                sizeof(struct wifi_join_request)

struct wifi_network_entry {
    char ssid[64];
    unsigned char bssid[6];
    int8_t rssi;
    unsigned char channel;
    unsigned char flags;
    unsigned char _padding;
};

struct wifi_join_request {
    char    ssid[64];
    char    key[64];
    unsigned char channel;
    unsigned char _padding;
};

/* SCSI INQUIRY Response Buffer Structure (Standard 36-byte response) */
struct scsi_inquiry_response {
    unsigned char peripheral_type; /* Device Type (0x03 = Processor) */
    unsigned char rmb;             /* Removable Media Bit */
    unsigned char version;         /* SCSI Version */
    unsigned char response_format;
    unsigned char additional_len;  /* Additional Length */
    unsigned char reserved[3];
    char          vendor_id[8];    /* Offset 8:  "Dayna   " */
    char          product_id[16];  /* Offset 16: "SCSI/Link       " */
    char          revision[4];     /* Offset 32: Firmware Revision */
};

extern int scsi_open(char *path, int readonly);
extern int scsi_close(int dev);

extern int scsi_send_command(int dev,
                             unsigned char *cmd,
                             int cmd_len,
                             unsigned char *buf,
                             int buf_len);

extern int scsi_send_commandw(int dev,
                              unsigned char *cmd,
                              int cmd_len,
                              unsigned char *buf,
                              int buf_len);

int verbose = 0;

/*
 * Build a 6-byte BlueSCSI Wi-Fi CDB.
 */
static void
wifi_make_cdb(unsigned char *cdb,
              unsigned char subcommand,
              unsigned short length)
{
    memset(cdb, 0, SCSI_CDB_LENGTH);

    cdb[0] = BLUESCSI_NETWORK_WIFI_CMD;
    cdb[1] = subcommand;

    cdb[3] = (unsigned char)((length >> 8) & 0xff);
    cdb[4] = (unsigned char)(length & 0xff);
}

/*
 * Send a Wi-Fi READ command.
 */
static int
wifi_read_command(int dev,
                  unsigned char subcommand,
                  unsigned char *buf,
                  int len)
{
    unsigned char cdb[SCSI_CDB_LENGTH];

    wifi_make_cdb(cdb, subcommand, (unsigned short)len);

    if (verbose)
    {
        int i;
        fprintf(stdout, "Wi-Fi CDB: ");
        for (i = 0; i < SCSI_CDB_LENGTH; ++i)
            fprintf(stdout, "%02x ", cdb[i]);
        fprintf(stdout, "\nWi-Fi READ length: %d\n", len);
    }

    memset(buf, 0, len);

    if (scsi_send_command(dev, cdb, sizeof(cdb), buf, len) != 0)
    {
        fprintf(stderr, "Wi-Fi READ command 0x%02x failed\n", subcommand);
        return -1;
    }

    return 0;
}

/*
 * Send command to start a Wi-Fi scan.
 */
static int
wifi_scan_start(int dev)
{
    unsigned char result = 0;

    if (wifi_read_command(dev, BLUESCSI_NETWORK_WIFI_CMD_SCAN, &result, 1) != 0)
    {
        return -1;
    }

    if (verbose)
    {
        printf("Raw SCAN response: %02x\n", (unsigned int)result);
    }

    if (result == 1)
    {
        return 0;
    }

    return -1;
}

/*
 * Check whether scan has completed.
 * Returns 1 if complete, 0 if still running.
 */
static int
wifi_is_complete(int dev)
{
    unsigned char result = 0;

    if (wifi_read_command(dev, BLUESCSI_NETWORK_WIFI_CMD_COMPLETE, &result, 1) != 0)
    {
        /* Match Macintosh behavior: assume finished if command errors out */
        return 1;
    }

    if (verbose)
    {
        printf("Raw COMPLETE response: %02x\n", (unsigned int)result);
    }

    return (result == 1) ? 1 : 0;
}

/*
 * Print an SSID.
 */
static void
print_ssid(const unsigned char *ssid)
{
    int i;
    for (i = 0; i < 64; ++i)
    {
        if (ssid[i] == 0)
            break;

        if (ssid[i] >= 32 && ssid[i] <= 126)
            putchar(ssid[i]);
        else
            putchar('.');
    }
}

/*
 * Print BSSID.
 */
static void
print_bssid(const unsigned char *bssid)
{
    printf("%02x:%02x:%02x:%02x:%02x:%02x",
           bssid[0], bssid[1], bssid[2],
           bssid[3], bssid[4], bssid[5]);
}

/*
 * Print one network entry.
 */
static void
print_network_entry(const unsigned char *entry, int number)
{
    signed char rssi;
    unsigned char channel;
    unsigned char flags;

    rssi = (signed char)entry[70];
    channel = entry[71];
    flags = entry[72];

    printf("%2d  ", number);
    print_ssid(entry);
    printf("\n    BSSID: ");
    print_bssid(entry + 64);
    printf("\n    RSSI: %d dBm\n", (int)rssi);
    printf("    Channel: %u\n", (unsigned int)channel);
    printf("    Auth: %s\n", (flags & WIFI_NETWORK_FLAG_AUTH) ? "yes" : "no");
}

/*
 * Dump raw data.
 */
static void
dump_hex(const unsigned char *buf, int len)
{
    int i;
    for (i = 0; i < len; ++i)
    {
        if ((i % 16) == 0)
            printf("%04x: ", i);

        printf("%02x ", buf[i]);

        if ((i % 16) == 15)
            printf("\n");
    }

    if ((len % 16) != 0)
        printf("\n");
}

/*
 * Fetch and print scan results.
 */
static int
wifi_results(int dev)
{
    unsigned char buf[(WIFI_NETWORK_ENTRY_SIZE * WIFI_NETWORK_ENTRY_COUNT) + 2];
    unsigned int size;
    unsigned int count;
    unsigned int i;

    memset(buf, 0, sizeof(buf));

    if (wifi_read_command(dev, BLUESCSI_NETWORK_WIFI_CMD_SCAN_RESULTS, buf, sizeof(buf)) != 0)
    {
        return -1;
    }

    size = ((unsigned int)buf[0] << 8) | (unsigned int)buf[1];

    if (verbose)
    {
        printf("Raw SCAN_RESULTS size: %u bytes\n", size);
        printf("First 128 bytes of SCAN_RESULTS response:\n");
        dump_hex(buf, 128);
    }

    if (size == 0)
    {
        printf("No Wi-Fi networks found.\n");
        return 0;
    }

    count = size / WIFI_NETWORK_ENTRY_SIZE;
    if (count > WIFI_NETWORK_ENTRY_COUNT)
        count = WIFI_NETWORK_ENTRY_COUNT;

    printf("Found %u network(s):\n\n", count);

    for (i = 0; i < count; ++i)
    {
        print_network_entry(buf + 2 + (i * WIFI_NETWORK_ENTRY_SIZE), i + 1);
        printf("\n");
    }

    return 0;
}

/*
 * Full scan sequence: Start scan -> Wait for completion -> Print results.
 */
static int
wifi_scan(int dev)
{
    int retries = 30; /* Up to 15 seconds (30 * 500ms) */

    fprintf(stdout,"Starting Wi-Fi scan...\n");

    if (wifi_scan_start(dev) != 0)
    {
        fprintf(stderr, "Error: Could not start Wi-Fi scan.\n");
        return -1;
    }

    fprintf(stdout,"Scanning");
    fflush(stdout);
    sleep(2);
    while (retries > 0)
    {
        usleep(500000); /* Wait 500ms between checks */

        if (wifi_is_complete(dev))
        {
            fprintf(stdout, " complete!\n\n");
            return wifi_results(dev);
        }

        fprintf(stdout, ".");
        fflush(stdout);
        retries--;
    }

    fprintf(stderr, "\nError: Wi-Fi scan timed out.\n");
    return -1;
}

/*
 * Get current Wi-Fi information.
 */
static int
wifi_info(int dev)
{
    unsigned char buf[sizeof(struct wifi_network_entry) + 2];
    unsigned int size;

    memset(buf, 0, sizeof(buf));

    if (wifi_read_command(dev, BLUESCSI_NETWORK_WIFI_CMD_INFO, buf, sizeof(buf)) != 0)
    {
        return -1;
    }

    size = ((unsigned int)buf[0] << 8) | (unsigned int)buf[1];

    if (verbose)
    {
        fprintf(stdout, "Raw INFO response (%d bytes):\n", (int)sizeof(buf));
        dump_hex(buf, sizeof(buf));
        fprintf(stdout, "\nINFO reported size: %u bytes\n", size);
    }

    if (size > WIFI_NETWORK_ENTRY_SIZE)
    {
	    fprintf(stderr, "WARNING: INFO returned size %u, expected at most %u\n", size, (unsigned int)WIFI_NETWORK_ENTRY_SIZE);}

    if (size < WIFI_NETWORK_ENTRY_SIZE)
    {
	    fprintf(stderr, "WARNING: INFO response is shorter than wifi_network_entry\n");
    }

    fprintf(stdout, "\nCurrent Wi-Fi network:\n");
    print_network_entry(buf + 2, 1);

    return 0;
}

/*
 * Join an access point and poll wifi_info until connected or timed out.
 */
static int
wifi_join(int dev, const char *ssid, const char *key, int channel)
{
    unsigned char cdb[SCSI_CDB_LENGTH];
    unsigned char request[WIFI_JOIN_REQUEST_SIZE];
    unsigned char buf[sizeof(struct wifi_network_entry) + 2];
    struct wifi_network_entry current_info;
    int retries = 20; /* 10 second timeout (20 * 500ms) */

    if (strlen(ssid) >= 64 || strlen(key) >= 64) {
        fprintf(stderr, "SSID or Key is too long (maximum 63 characters)\n");
        return 1;
    }

    if (channel < 0 || channel > 255) {
        fprintf(stderr, "Channel must be between 0 and 255\n");
        return 1;
    }

    memset(request, 0, sizeof(request));
    memcpy(request, ssid, strlen(ssid));
    memcpy(request + 64, key, strlen(key));
    request[128] = (unsigned char)channel;

    wifi_make_cdb(cdb, BLUESCSI_NETWORK_WIFI_CMD_JOIN, WIFI_JOIN_REQUEST_SIZE);

    if (scsi_send_commandw(dev, cdb, sizeof(cdb), request, sizeof(request)) != 0) {
        fprintf(stderr, "Wi-Fi JOIN command failed\n");
        return -1;
    }

    printf("Connecting to \"%s\"", ssid);
    fflush(stdout);

    /* Poll wifi_info to check when connected */
    while (retries > 0) {
        usleep(500000); /* Wait 500ms */

        /* Issue INFO command to check current network status */
        if (wifi_read_command(dev, BLUESCSI_NETWORK_WIFI_CMD_INFO, buf, sizeof(buf)) == 0) {
            memcpy(&current_info, buf + 2, sizeof(current_info));

            /* Check if connected SSID matches requested SSID */
            if (strncmp(current_info.ssid, ssid, strlen(ssid)) == 0) {
                printf(" Connected!\n");
                return 0;
            }
        }

        printf(".");
        fflush(stdout);
        retries--;
    }

    printf("\nTimed out waiting to join network.\n");
    return -1;
}

/*
 * Interrogates an open SCSI device to verify it is a DaynaPort adapter with Wi-Fi support.
 */
static int 
check_scsi_inquiry(int dev)
{
    struct scsi_inquiry_response inq;
    const char *dp_vendor_id = "Dayna";
    const char *dp_product_id = "SCSI/Link";
    unsigned char cdb_inq[6] = { 0x12, 0x00, 0x00, 0x00, sizeof(inq), 0x00 };
    char vendor[9];
    char product[17];
    int ret;

    memset(&inq, 0, sizeof(inq));

    ret = scsi_send_command(dev, cdb_inq, 6, (unsigned char *)&inq, sizeof(inq));
    if (ret != 0)
    {
        if (verbose)
            fprintf(stderr, "check_scsi_inquiry: INQUIRY command failed (ret=%d)\n", ret);
        return 1;
    }

    memcpy(vendor, inq.vendor_id, 8);
    vendor[8] = '\0';
    memcpy(product, inq.product_id, 16);
    product[16] = '\0';

    if (verbose)
        fprintf(stdout, "SCSI INQUIRY Vendor: \"%s\", Product: \"%s\"\n", vendor, product);

    if (memcmp(vendor, dp_vendor_id, sizeof(dp_vendor_id)) != 0 || memcmp(product, dp_product_id, sizeof(dp_product_id)) != 0)
    {
        fprintf(stderr, "Device is not a DaynaPort SCSI/Link adapter: %s %s\n", vendor, product);
        return 1;
    }
    return 0;
}

/*
 * Usage.
 */
static void
usage(void)
{
    fprintf(stderr,
        "\n"
        "Usage:\n"
        "  bswifi [-v] <device> scan\n"
        "  bswifi [-v] <device> info\n"
        "  bswifi [-v] <device> join <ssid> <key> [channel]\n"
        "\n"
        "Commands:\n"
        "  scan                  Scan for Wi-Fi networks and display results\n"
        "  info                  Get current Wi-Fi connection info\n"
        "  join SSID KEY [CHAN]  Join an access point\n"
        "\n"
        "Options:\n"
        "  -v                    Verbose/debug output\n"
        "  -h                    Show this help\n"
	"\n\nExample:\n\n sudo ./bswifi dp0 join MYNETWORK MYPASSWORD\n"
	"\nPlease make sure you run the program as root.\n"
        "\n");
}

/*
 * Main.
 */
int
main(int argc, char *argv[])
{
    int c;
    int dev;
    int ret = 1;

    char device[255];
    const char *command;

    while ((c = getopt(argc, argv, "vh")) != -1)
    {
        switch (c)
        {
            case 'v':
                verbose = 1;
                break;
            case 'h':
                usage();
                return 0;
            default:
                usage();
                return 1;
        }
    }

    argc -= optind;
    argv += optind;

    if (argc < 2)
    {
	fprintf (stderr, "Error: not enough parameters given\n");
        usage();
        return 1;
    }

    if (get_scsi_path_for_iface(argv[0], device, sizeof(device)) == 0) 
    {
        if (verbose)
            fprintf(stdout, "%s maps to: %s\n", argv[0], device);
    } 
    else 
    {
        fprintf(stderr, "Could not find SCSI device for %s\n", argv[0]);
        return 1;
    } 

    command = argv[1];

    dev = scsi_open((char *)device, 0);
    
    if (dev < 0)
    {
        fprintf(stderr, "ERROR: Cannot open %s: %s\n", device, strerror(errno));
        return 1;
    }

    if (check_scsi_inquiry(dev) != 0)
    {
        fprintf(stderr, "Couldn't find wifi capabilities on %s\n", device);
        scsi_close(dev);
        return 1;
    }

    if (strcmp(command, "scan") == 0)
    {
        if (argc != 2)
        {
            usage();
            ret = 1;
        }
        else
        {
            ret = wifi_scan(dev);
        }
    }
    else if (strcmp(command, "info") == 0)
    {
        if (argc != 2)
        {
            usage();
            ret = 1;
        }
        else
        {
            ret = wifi_info(dev);
        }
    }
    else if (strcmp(command, "join") == 0)
    {
        int channel;

        if (argc < 4 || argc > 5)
        {
            usage();
            ret = 1;
        }
        else
        {
            channel = 0;
            if (argc == 5)
                channel = atoi(argv[4]);

            ret = wifi_join(dev, argv[2], argv[3], channel);
        }
    }
    else
    {
        fprintf(stderr, "Unknown command: %s\n", command);
        usage();
        ret = 1;
    }

    scsi_close(dev);
    return ret;
}

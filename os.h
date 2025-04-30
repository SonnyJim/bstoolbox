int scsi_open(char *path);
int scsi_send_command(int dev, unsigned char *cmd, int cmd_len, unsigned char *buf, int buf_len);
int scsi_send_commandw(int dev, unsigned char *cmd, int cmd_len, unsigned char *buf, int buf_len);
int scsi_close(int dev);

int path_to_devnum(const char *path);

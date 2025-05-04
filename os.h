int scsi_open(char *path, int readonly);
int scsi_send_command(int dev, unsigned char *cmd, int cmd_len, unsigned char *buf, int buf_len);
int scsi_send_commandw(int dev, unsigned char *cmd, int cmd_len, unsigned char *buf, int buf_len);
int scsi_close(int dev);

int path_to_devnum(const char *path);

int mediad_start(void); //Helper functions to start and stop the removable device damons
int mediad_stop(void);

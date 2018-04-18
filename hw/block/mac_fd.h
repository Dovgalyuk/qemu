#define BLOCK_SIZE 512

void mac_fd_read(int disk_offset, int ram_offset, int count);

void mac_fd_write(int disk_offset, int ram_offset, int count);
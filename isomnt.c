#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/mount.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <linux/loop.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/vfs.h>

// Function to check if a file exists
bool check_file_exists(char *filename) {
    struct stat buf;
    return (stat(filename, &buf) == 0);
}

// Function to check if a directory exists
bool check_directory_exists(char *directory) {
    struct stat buf;
    return (stat(directory, &buf) == 0 && S_ISDIR(buf.st_mode));
}

// Function to check the file extensions of the iso image
bool check_isoimage(char *filename) {
    char *extension = strrchr(filename, '.');
    if (!extension) {
        return false;
    } else {
        if (strncmp(extension + 1, "iso", 4) == 0) {
            return true;
        } else {
            return false;
        }
    }
}

// Function to validate a filesystem
bool check_iso_fs(char *target) {
    struct statfs buf;
    if (statfs(target, &buf)) {
        return false;
    }
    // 0x9660 == ISO9660 File System Magic
    // manual page: man statfs 2
    if (buf.f_type == 0x9660) {
        return true;
    } else {
        return false;
    }
}

// Function to check if a mountpoint exists
bool check_mountpoint_exists(char *target) {
    FILE *fp = fopen("/proc/mounts", "r");
    if (fp == NULL) {
        perror("fopen");
        exit(1);
    }

    char line[1024];
    // Read every partition line by line
    while (fgets(line, sizeof(line), fp) != NULL) {
        char *copy = strdup(line); // Make a temporary copy of the line
        char *mountpoint = strtok(copy, " "); // Get the mountpoint
        mountpoint = strtok(NULL, " ");

        // A mountpoint exists
        if (strncmp(mountpoint, target, strlen(target)) == 0) {
            fclose(fp);
            free(copy);
            return true;
        }
        free(copy);
    }
    fclose(fp);
    return false;
}

// Function to mount ISO images
void mount_iso(char *filename, char *target) {
    // Check if the ISO image exists
    bool isofile_exists = check_file_exists(filename);
    if (isofile_exists == false) {
        printf(".iso image doesn't exist\n");
        exit(1);
    }

    // Check if the ISO file is a valid ISO file
    bool is_isofile = check_isoimage(filename);
    if (is_isofile == false) {
        printf("Not a valid .iso image\n");
        exit(1);
    }

    // Check if the target mountpoint exists
    bool is_target = check_directory_exists(target);
    if (is_target == false) {
        printf("Target mountpoint doesn't exist\n");
        exit(1);
    }

    // Check if a mountpoint exists
    bool is_mountpoint = check_mountpoint_exists(target);
    if (is_mountpoint == true) {
        printf("A mountpoint exists\n");
        exit(1);
    }


    printf("Mounting .iso image: %s\n", filename);
    int file_fd, device_fd, loop_control_fd;

    // Open loop-control file
    loop_control_fd = open("/dev/loop-control", O_RDWR);
    if (loop_control_fd < -1) {
        perror("opening loop control file failed");
        exit(1);
    }

    // Open backing file
    file_fd = open(filename, O_RDWR);
    if (file_fd < -1) {
        perror("opening backing file failed");
        close(loop_control_fd);
        exit(1);
    }

    // Create a variable to store the loop device name
    char *loop_name = malloc(50 * sizeof(char));
    if (!loop_name) {
        perror("malloc");
        close(loop_control_fd);
        close(file_fd);
        exit(1);
    }
    memset(loop_name, 0, 50 * sizeof(char));

    // Find a free available loop device number
    int loopnum = ioctl(loop_control_fd, LOOP_CTL_GET_FREE);
    if (loopnum < 0) {
        perror("ioctl LOOP_CTL_GET_FREE failed");
        close(loop_control_fd);
        close(file_fd);
        free(loop_name);
        exit(1);
    }
    // Construct loop device name
    snprintf(loop_name, 50 * sizeof(char), "%s%d", "/dev/loop", loopnum);

    // Open loop device
    device_fd = open(loop_name, O_RDWR);
    if (device_fd < -1) {
        perror("opening loop device failed");
        close(loop_control_fd);
        close(file_fd);
        free(loop_name);
        exit(1);
    }

    // Associate the loop device
    if (ioctl(device_fd, LOOP_SET_FD, file_fd) < 0) {
        perror("ioctl LOOP_SET_FD failed");
        close(loop_control_fd);
        close(file_fd);
        close(device_fd);
        free(loop_name);
        exit(1);
    }
    close(loop_control_fd);
    close(file_fd);
    close(device_fd);

    // Try mounting the ISO file
    int ret = mount(loop_name, target, "iso9660", MS_RDONLY, NULL);
    free(loop_name);

    if (ret == -1) {
        perror("mount");
        exit(1);
    } else {
        printf("Succesfully mounted .iso image: %s\n", filename);
    }
}

// Function to unmount ISO image
void unmount_iso(char *target) {
    // Check if the target mountpoint directory exists
    bool is_target = check_directory_exists(target);
    if (is_target == false) {
        printf("Target mountpoint doesn't exist\n");
        exit(1);
    }

    // Check if a mountpoint exists
    bool is_mountpoint = check_mountpoint_exists(target);
    if (is_mountpoint == false) {
        printf("Nothing is mounted\n");
        exit(1);
    }

    // Check for a valid filesystem
    bool is_isofs = check_iso_fs(target);
    if (is_isofs == false) {
        printf("Target mountpoint uses an invalid filesystem type\n");
        printf("Only ISO9660 is supported\n");
        exit(1);
    }

    printf("Unmounting .iso image from: %s\n", target);

    // Get the loop device name from the list of mounted devices
    FILE *fp = fopen("/proc/mounts", "r");
    if (fp == NULL) {
        perror("fopen");
        exit(1);
    }

    char line[1024];
    // Read every partition line by line
    while (fgets(line, sizeof(line), fp) != NULL) {
        char *copy = strdup(line); // Make a temporary copy of the line
        char *block = strtok(copy, " "); // Get the block device name
        char *mountpoint = strtok(NULL, " "); // Get the mountpoint

        // If the found mountpoint is equal to the mountpoint we want
        // to unmount then firstly disassociate the loop device
        if (strncmp(mountpoint, target, strlen(target)) == 0) {
            int device_fd = open(block, O_RDWR);
            if (ioctl(device_fd, LOOP_CLR_FD, 0) < 0) {
                perror("ioctl LOOP_CLR_FD failed");
                fclose(fp);
                free(copy);
                exit(1);
            }
            close(device_fd);
            free(copy);
            break;
        }
        free(copy);
    }
    fclose(fp);

    // Try to unmount the .iso image
    int ret = umount(target);
    if (ret == -1) {
        perror("umount");
        exit(1);
    } else {
        printf("Unmounted .iso image from: %s\n", target);
    }
}

void check_root() {
    if (geteuid() != 0) {
        printf("root privileges required\n");
        exit(1);
    }
}

void usage(void) {
    printf("Usage: isomnt [ISO] [TARGET]\n");
    printf("  -m  => mount an .iso image\n");
    printf("      => requires BOTH arguments\n\n");
    printf("  -u  => unmount an .iso image\n");
    printf("      => requires only the TARGET argument\n");
}

int main(int argc, char *argv[]) {
    // No option given
    if (argc < 2) {
        usage();
        exit(1);
    } else {
        // -m flag -> Mount flag
        if (strncmp(argv[1], "-m", 3) == 0) {
            // If both the ISO and TARGET arguments are given
            if (argc == 4) {
                check_root();
                mount_iso(argv[2], argv[3]);
            } else if (argc > 4) {
                printf("Too many arguments\n");
                usage();
                exit(1);
            } else {
                printf("Not enough arguments\n");
                usage();
                exit(1);
            }
        // -u flag -> Unmount flag
        } else if (strncmp(argv[1], "-u", 3) == 0) {
            // If the TARGET argument was given
            if (argc == 3) {
                check_root();
                unmount_iso(argv[2]);
            } else if (argc > 3) {
                printf("Too many arguments\n");
                usage();
                exit(1);
            } else {
                printf("Not enough arguments\n");
                usage();
                exit(1);
            }
        } else {
            printf("Invalid option\n");
            usage();
            exit(1);
        }
    }

    return 0;
}
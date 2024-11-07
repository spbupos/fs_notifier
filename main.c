#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fsnotify.h>
#include <linux/fsnotify_backend.h>
#include <linux/init.h>
#include <linux/timekeeping.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/debugfs.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include "header.h"


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("Extended R/W access monitoring module");
MODULE_VERSION("0.1.0");


static void log_file_event(struct inode *inode, const char *full_path, size_t file_size) {
    char dev_path[32];
    struct timespec64 ts;
    ssize_t bytes_read;
    int operation_status = 0;
    char file_content[100] = {0}; // Buffer for the portion of file content
    struct file *file;

    // Get current timestamp in ns
    ktime_get_real_ts64(&ts);
    s64 timestamp_ns = timespec64_to_ns(&ts);

    // Get device path (for example, /dev/sda1)
    snprintf(dev_path, sizeof(dev_path), "/dev/%s", inode->i_sb->s_id);

    // Read the middle part of the file
    file = filp_open(full_path, O_RDONLY, 0);
    if (file_size > 20) {
        loff_t offset = file_size > 200 ? file_size / 2 - 50 : 10; // Middle of file or fixed for small files
        size_t read_size = file_size > 200 ? 100 : 20;
        bytes_read = kernel_read(file, file_content, read_size, &offset);
    } else {
        loff_t offset = 0;
        bytes_read = kernel_read(file, file_content, file_size, &offset);
    }

    // Check if reading was successful
    if (bytes_read < 0) {
        operation_status = bytes_read; // Store error code
        bytes_read = 0;                // No data read
    }

    // 4. Store log data in binary format in log_buffer
    if (log_index < MAX_LOG_ENTRIES) {
        snprintf(log_buffer + log_index * LOG_ENTRY_SIZE, LOG_ENTRY_SIZE,
                 "Timestamp(ns): %lld\nDevice: %s\nPath: %s\nData: %.*s\nStatus: %d\n\n",
                 timestamp_ns, dev_path, full_path, (int)bytes_read, file_content, operation_status);
        log_index++;
    }

}


static int monitor_event_handler(struct fsnotify_group *group, u32 mask, const void *data, int data_type,
                                 struct inode *dir, const struct qstr *file_name, u32 cookie,
                                 struct fsnotify_iter_info *iter_info) {
    struct inode *inode = NULL;
    struct dentry *dentry = NULL;
    char full_path[256];
    size_t file_size = 0;

    // Get the inode from the data
    if (!data) {
        return 0;
    }
    switch (data_type) {
        case FSNOTIFY_EVENT_INODE:
            inode = (struct inode *) data;
            break;
        case FSNOTIFY_EVENT_DENTRY:
            dentry = (struct dentry *) data;
            inode = dentry->d_inode;
            break;
        default:
            break;
    }

    // Get the full path of the file
    if (dentry) {
        snprintf(full_path, sizeof(full_path), "%p/%p", dentry->d_parent->d_name.name, dentry->d_name.name);
    } else {
        snprintf(full_path, sizeof(full_path), "%s/%p", dir->i_sb->s_id, file_name->name);
    }

    // Get the file size
    if (inode) {
        file_size = i_size_read(inode);
    }

    // Log the file event
    printk(KERN_INFO "File event: %s\n", full_path);
    log_file_event(inode, full_path, file_size);

    return 0;
}


static const struct fsnotify_ops fsnotify_ops = {
    .handle_event = monitor_event_handler,
};


// Read function to output `log_buffer` to user space
static ssize_t proc_read_log(struct file *file, char __user *buf, size_t len, loff_t *ppos) {
    size_t log_size = log_index * LOG_ENTRY_SIZE;
    ssize_t ret;

    // Ensure we're not trying to read beyond the buffer
    if (*ppos >= log_size)
        return 0;

    // Copy data from kernel space to user space
    ret = simple_read_from_buffer(buf, len, ppos, log_buffer, log_size);

    return ret;
}

// Define file operations for the proc entry
static const struct proc_ops proc_fops = {
    .proc_read = proc_read_log,
};


static int __init fs_monitor_init(void) {
    struct vfsmount *mnt;
    struct list_head *p;
    printk(KERN_INFO "Initializing FS Monitor Module\n");

    // Set up your device or proc file for logging data
    proc_entry = proc_create(PROC_FILE_NAME, 0444, NULL, &proc_fops);
    if (!proc_entry) {
        printk(KERN_ERR "Failed to create /proc/%s\n", PROC_FILE_NAME);
        return -ENOMEM;
    }
    printk(KERN_INFO "/proc/%s created for file monitoring logs\n", PROC_FILE_NAME);

    // iterate over all mounted filesystems and monitor them
    monitor_group = fsnotify_alloc_group(&fsnotify_ops, 0);
    // HERE IS SOMETHING FOR MONITORING, IDK WHAT

    // DEBUG: print something to log_buffer to check if printing to proc works
    snprintf(log_buffer, LOG_ENTRY_SIZE, "Hello, world!\n");

    printk(KERN_INFO "FS Monitor Module Initialized\n");
    return 0;
}


static void __exit fs_monitor_exit(void) {
    printk(KERN_INFO "Exiting FS Monitor Module\n");

    // Clean up
    proc_remove(proc_entry);
    fsnotify_put_group(monitor_group);
}


module_init(fs_monitor_init);
module_exit(fs_monitor_exit);

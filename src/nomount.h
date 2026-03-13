/*
 * NoMountFS: A stackable file system for Android content redirection.
 *   Based on WrapFS by Erez Zadok.
 *   This file contains the private declarations for nomountfs.
 */

#ifndef _NOMOUNT_H_
#define _NOMOUNT_H_

#include <linux/dcache.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/mount.h>
#include <linux/namei.h>
#include <linux/seq_file.h>
#include <linux/statfs.h>
#include <linux/fs_stack.h>
#include <linux/magic.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/xattr.h>
#include <linux/exportfs.h>

/* The file system name for 'mount -t nomountfs' */
#define NOMOUNT_NAME "nomountfs"

/* Nomountfs root inode number */
#define NOMOUNT_ROOT_INO     1

/* Nomountfs magic number */
#define NOMOUNT_FS_MAGIC 0xF18F

/* Operations vectors defined in specific files */
extern const struct file_operations nomount_main_fops;
extern const struct file_operations nomount_dir_fops;
extern const struct inode_operations nomount_main_iops;
extern const struct inode_operations nomount_dir_iops;
extern const struct inode_operations nomount_symlink_iops;
extern const struct super_operations nomount_sops;
extern const struct dentry_operations nomount_dops;
extern const struct address_space_operations nomount_aops, nomount_dummy_aops;
extern const struct vm_operations_struct nomount_vm_ops;
extern const struct export_operations nomount_export_ops;
extern const struct xattr_handler *nomount_xattr_handlers[];

/* Cache and lifecycle management functions */
extern int nomount_init_inode_cache(void);
extern void nomount_destroy_inode_cache(void);
extern int nomount_init_dentry_cache(void);
extern void nomount_destroy_dentry_cache(void);
extern int new_dentry_private_data(struct dentry *dentry);
extern void free_dentry_private_data(struct dentry *dentry);

/* Core lookup and interpose functions */
extern struct dentry *nomount_lookup(struct inode *dir, struct dentry *dentry,
				      				  unsigned int flags);
extern struct inode *nomount_iget(struct super_block *sb, struct inode *lower_inode);
extern struct dentry *__nomount_interpose(struct dentry *dentry,
					 					  struct super_block *sb,
					 					  struct path *lower_path);
extern int nomount_fill_super(struct super_block *sb, void *raw_data, int silent);
extern int nomount_statfs(struct dentry *dentry, struct kstatfs *buf);
#if defined(HAVE_ITERATE_SHARED) || defined(HAVE_ITERATE)
extern int nomount_iterate(struct file *file, struct dir_context *ctx);
#else
extern int nomount_readdir(struct file *file, void *dirent, filldir_t filldir);
#endif
extern int nomount_mmap(struct file *file, struct vm_area_struct *vma);
extern int nomount_test_inode(struct inode *inode, void *data);
extern int nomount_set_inode(struct inode *inode, void *data);

/* File private data: link to the real underlying file */
struct nomount_file_info {
	struct file *lower_file;
	const struct vm_operations_struct *lower_vm_ops;
	bool ghost_emitted;
};

/* Nomountfs inode data in memory */
struct nomount_inode_info {
	struct inode *lower_inode;
	struct inode vfs_inode;
};

/* Nomountfs dentry data in memory */
struct nomount_dentry_info {
	spinlock_t lock;	/* Protects lower_path */
	struct path lower_path;
};

/* Nomountfs super-block data in memory */
struct nomount_sb_info {
	struct super_block *lower_sb;
	struct path lower_path;
	bool has_inject;
	char inject_name[NAME_MAX]; /* Name of the file to intercept */
	struct path inject_path;    /* Actual path of the modified file */
};

/* Structure to safely pass assembly data to fill_super */
struct nomount_mount_data {
	const char *dev_name;
	void *raw_data;
};

/*
 * Inode to private data conversion.
 * Since struct inode is embedded, this will always return a valid pointer.
 */
static inline struct nomount_inode_info *NOMOUNT_I(const struct inode *inode)
{
	return container_of(inode, struct nomount_inode_info, vfs_inode);
}

/* Helper macros for accessing private data */
#define NOMOUNT_D(dent) ((struct nomount_dentry_info *)(dent)->d_fsdata)
#define NOMOUNT_SB(super) ((struct nomount_sb_info *)(super)->s_fs_info)
#define NOMOUNT_F(file) ((struct nomount_file_info *)((file)->private_data))

/* Helper functions to get/set lower (real) objects */
static inline struct file *nomount_lower_file(const struct file *f)
{
	return NOMOUNT_F(f)->lower_file;
}

static inline struct inode *nomount_lower_inode(const struct inode *i)
{
	return NOMOUNT_I(i)->lower_inode;
}

static inline struct super_block *nomount_lower_super(const struct super_block *sb)
{
	return NOMOUNT_SB(sb)->lower_sb;
}

/* Copying and handling lower paths safely */
static inline void pathcpy(struct path *dst, const struct path *src)
{
	dst->dentry = src->dentry;
	dst->mnt = src->mnt;
}

/* Safely retrieve the lower path while holding the dentry lock */
static inline void nomount_get_lower_path(const struct dentry *dent, struct path *lower_path)
{
	spin_lock(&NOMOUNT_D(dent)->lock);
	pathcpy(lower_path, &NOMOUNT_D(dent)->lower_path);
	path_get(lower_path);
	spin_unlock(&NOMOUNT_D(dent)->lock);
}

static inline void nomount_put_lower_path(const struct dentry *dent, struct path *lower_path)
{
	path_put(lower_path);
}

/* Helpers to set private data  */
static inline void nomount_set_lower_inode(struct inode *inode, struct inode *lowernode)
{
	NOMOUNT_I(inode)->lower_inode = lowernode;
}

static inline void nomount_set_lower_path(struct dentry *dent, struct path *lower_path)
{
	pathcpy(&NOMOUNT_D(dent)->lower_path, lower_path);
}

/* Locking helpers for directory operations */
static inline struct dentry *lock_parent(struct dentry *dentry)
{
	struct dentry *dir = dget_parent(dentry);
	inode_lock(d_inode(dir));
	return dir;
}

static inline void unlock_dir(struct dentry *dir)
{
	inode_unlock(d_inode(dir));
	dput(dir);
}

#endif	/* _NOMOUNT_H_ */

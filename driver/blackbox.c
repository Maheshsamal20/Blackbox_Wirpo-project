// SPDX-License-Identifier: GPL-2.0
/*
 * blackbox.c - BlackBox flight recorder: ring buffer exposed as /dev/blackbox
 *
 *  - write(): record an event ("<N>text", N = 0..3 selects the level)
 *  - read():  returns whole struct bb_event records, per-file cursor
 *  - poll():  readable when unread events exist
 *  - ioctl(): FREEZE / UNFREEZE / CLEAR / GET_STATS / SEEK_OLDEST
 *  - /proc/blackbox: human readable status
 *  - optional heartbeat event from a kernel timer (heartbeat_ms)
 *
 * Locking: a single irq-safe spinlock (bb.lock) protects all ring state
 * because the timer callback also pushes events. User-space copies are
 * never done with the lock held.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/uaccess.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/ktime.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/version.h>

#include "bb_uapi.h"

#define DRV_NAME "blackbox"

static unsigned int buf_events = 1024;
module_param(buf_events, uint, 0444);
MODULE_PARM_DESC(buf_events, "Ring buffer capacity in events (16..1048576, default 1024)");

static unsigned int heartbeat_ms;
module_param(heartbeat_ms, uint, 0444);
MODULE_PARM_DESC(heartbeat_ms, "Heartbeat event period in ms (0 = disabled, default 0)");

struct bb_dev {
	dev_t devt;
	struct cdev cdev;
	struct class *cls;
	struct device *dev;
	struct proc_dir_entry *proc;
	struct timer_list hb_timer;
	bool stopping;                 /* set on exit so the timer does not re-arm */

	spinlock_t lock;               /* protects everything below */
	struct bb_event *events;       /* ring storage */
	u32 capacity;
	u64 head_seq;                  /* next sequence number to assign */
	u64 base_seq;                  /* events older than this are cleared */
	u64 total_written;
	u64 dropped_frozen;
	bool frozen;
	wait_queue_head_t wq;          /* readers wait here for new events */
};

/* Per-open-file state: each reader has its own cursor. */
struct bb_session {
	u64 next_seq;
};

static struct bb_dev bb;

/* ---- ring helpers (caller holds bb.lock) ------------------------------- */

static u64 bb_oldest_locked(void)
{
	u64 oldest = 0;

	if (bb.head_seq > bb.capacity)
		oldest = bb.head_seq - bb.capacity;
	return oldest > bb.base_seq ? oldest : bb.base_seq;
}

static void bb_push_locked(const struct bb_event *src)
{
	struct bb_event *slot = &bb.events[bb.head_seq % bb.capacity];

	*slot = *src;
	slot->seq = bb.head_seq;
	bb.head_seq++;
	bb.total_written++;
}

/* ---- heartbeat timer --------------------------------------------------- */

static void bb_heartbeat(struct timer_list *t)
{
	struct bb_event ev;
	unsigned long flags;
	bool pushed = false;

	memset(&ev, 0, sizeof(ev));
	ev.ts_ns = ktime_get_real_ns();
	ev.level = BB_LVL_DEBUG;
	ev.source = BB_SRC_HEARTBEAT;

	spin_lock_irqsave(&bb.lock, flags);
	if (!bb.frozen) {
		ev.len = scnprintf(ev.msg, BB_MSG_MAX, "heartbeat #%llu",
				   (unsigned long long)bb.head_seq);
		bb_push_locked(&ev);
		pushed = true;
	}
	spin_unlock_irqrestore(&bb.lock, flags);

	if (pushed)
		wake_up_interruptible(&bb.wq);

	if (!READ_ONCE(bb.stopping))
		mod_timer(&bb.hb_timer, jiffies + msecs_to_jiffies(heartbeat_ms));
}

static void bb_timer_stop(void)
{
	WRITE_ONCE(bb.stopping, true);
	smp_mb();
	/*
	 * A handler that raced with the flag may have re-armed the timer once;
	 * the second call removes that and waits for any final run.
	 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 2, 0)
	timer_delete_sync(&bb.hb_timer);
	timer_delete_sync(&bb.hb_timer);
#else
	del_timer_sync(&bb.hb_timer);
	del_timer_sync(&bb.hb_timer);
#endif
}

/* ---- file operations --------------------------------------------------- */

static int bb_open(struct inode *inode, struct file *file)
{
	struct bb_session *s = kzalloc(sizeof(*s), GFP_KERNEL);
	unsigned long flags;

	if (!s)
		return -ENOMEM;

	nonseekable_open(inode, file);     /* a ring buffer has no file offset */

	spin_lock_irqsave(&bb.lock, flags);
	s->next_seq = bb.head_seq;     /* new readers see only new events */
	spin_unlock_irqrestore(&bb.lock, flags);

	file->private_data = s;
	return 0;
}

static int bb_release(struct inode *inode, struct file *file)
{
	kfree(file->private_data);
	return 0;
}

/*
 * write(): the buffer is one text message. An optional "<N>" prefix
 * (N = 0..3) selects the level. Long messages are truncated.
 * Returns -EBUSY while the recorder is frozen.
 */
static ssize_t bb_write(struct file *file, const char __user *ubuf,
			size_t count, loff_t *ppos)
{
	struct bb_event ev;
	char tmp[BB_MSG_MAX];
	size_t n, off = 0;
	unsigned long flags;
	u8 level = BB_LVL_INFO;

	if (count == 0)
		return 0;

	n = min_t(size_t, count, BB_MSG_MAX - 1);
	if (copy_from_user(tmp, ubuf, n))
		return -EFAULT;
	tmp[n] = '\0';

	/* optional level prefix: "<N>" */
	if (n >= 3 && tmp[0] == '<' && tmp[1] >= '0' && tmp[1] <= '3' &&
	    tmp[2] == '>') {
		level = tmp[1] - '0';
		off = 3;
	}
	/* strip trailing newlines */
	while (n > off && (tmp[n - 1] == '\n' || tmp[n - 1] == '\r'))
		tmp[--n] = '\0';

	memset(&ev, 0, sizeof(ev));
	ev.ts_ns = ktime_get_real_ns();
	ev.pid = current->pid;
	ev.level = level;
	ev.source = BB_SRC_USER;
	ev.len = n - off;
	memcpy(ev.msg, tmp + off, ev.len);

	spin_lock_irqsave(&bb.lock, flags);
	if (bb.frozen) {
		bb.dropped_frozen++;
		spin_unlock_irqrestore(&bb.lock, flags);
		return -EBUSY;
	}
	bb_push_locked(&ev);
	spin_unlock_irqrestore(&bb.lock, flags);

	wake_up_interruptible(&bb.wq);
	return count;                  /* tell the caller everything was consumed */
}

/* read(): returns whole events starting at this file's cursor. */
static ssize_t bb_read(struct file *file, char __user *ubuf,
		       size_t count, loff_t *ppos)
{
	struct bb_session *s = file->private_data;
	struct bb_event ev;
	size_t done = 0;
	unsigned long flags;

	if (count < sizeof(ev))
		return -EINVAL;

	while (done + sizeof(ev) <= count) {
		bool have = false;
		u64 oldest;

		spin_lock_irqsave(&bb.lock, flags);
		oldest = bb_oldest_locked();
		if (s->next_seq < oldest)
			s->next_seq = oldest;      /* overwritten: skip ahead */
		if (s->next_seq < bb.head_seq) {
			ev = bb.events[s->next_seq % bb.capacity];
			s->next_seq++;
			have = true;
		}
		spin_unlock_irqrestore(&bb.lock, flags);

		if (!have) {
			if (done)
				break;             /* return what we have */
			if (file->f_flags & O_NONBLOCK)
				return -EAGAIN;
			if (wait_event_interruptible(bb.wq,
				READ_ONCE(s->next_seq) < READ_ONCE(bb.head_seq)))
				return -ERESTARTSYS;
			continue;
		}

		if (copy_to_user(ubuf + done, &ev, sizeof(ev)))
			return done ? done : -EFAULT;
		done += sizeof(ev);
	}
	return done;
}

static __poll_t bb_poll(struct file *file, poll_table *wait)
{
	struct bb_session *s = file->private_data;
	__poll_t mask = EPOLLOUT | EPOLLWRNORM;   /* writes never block */
	unsigned long flags;

	poll_wait(file, &bb.wq, wait);

	spin_lock_irqsave(&bb.lock, flags);
	if (s->next_seq < bb.head_seq)
		mask |= EPOLLIN | EPOLLRDNORM;
	spin_unlock_irqrestore(&bb.lock, flags);
	return mask;
}

static long bb_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct bb_session *s = file->private_data;
	unsigned long flags;
	struct bb_stats st;

	switch (cmd) {
	case BB_IOC_FREEZE:
		spin_lock_irqsave(&bb.lock, flags);
		bb.frozen = true;
		spin_unlock_irqrestore(&bb.lock, flags);
		return 0;

	case BB_IOC_UNFREEZE:
		spin_lock_irqsave(&bb.lock, flags);
		bb.frozen = false;
		spin_unlock_irqrestore(&bb.lock, flags);
		return 0;

	case BB_IOC_CLEAR:
		spin_lock_irqsave(&bb.lock, flags);
		bb.base_seq = bb.head_seq;          /* older events become invalid */
		bb.total_written = 0;
		bb.dropped_frozen = 0;
		spin_unlock_irqrestore(&bb.lock, flags);
		return 0;

	case BB_IOC_SEEK_OLDEST:
		spin_lock_irqsave(&bb.lock, flags);
		s->next_seq = bb_oldest_locked();
		spin_unlock_irqrestore(&bb.lock, flags);
		return 0;

	case BB_IOC_GET_STATS:
		memset(&st, 0, sizeof(st));
		spin_lock_irqsave(&bb.lock, flags);
		st.oldest_seq = bb_oldest_locked();
		st.head_seq = bb.head_seq;
		st.used = bb.head_seq - st.oldest_seq;
		st.total_written = bb.total_written;
		st.dropped_frozen = bb.dropped_frozen;
		st.capacity = bb.capacity;
		st.frozen = bb.frozen;
		spin_unlock_irqrestore(&bb.lock, flags);
		if (copy_to_user((void __user *)arg, &st, sizeof(st)))
			return -EFAULT;
		return 0;

	default:
		return -ENOTTY;
	}
}

static const struct file_operations bb_fops = {
	.owner          = THIS_MODULE,
	.open           = bb_open,
	.release        = bb_release,
	.read           = bb_read,
	.write          = bb_write,
	.poll           = bb_poll,
	.unlocked_ioctl = bb_ioctl,
};

/* ---- /proc/blackbox ---------------------------------------------------- */

static int bb_proc_show(struct seq_file *m, void *v)
{
	unsigned long flags;
	u64 head, oldest, total, dropped;
	bool frozen;

	spin_lock_irqsave(&bb.lock, flags);
	head = bb.head_seq;
	oldest = bb_oldest_locked();
	total = bb.total_written;
	dropped = bb.dropped_frozen;
	frozen = bb.frozen;
	spin_unlock_irqrestore(&bb.lock, flags);

	seq_printf(m, "capacity:       %u events\n", bb.capacity);
	seq_printf(m, "used:           %llu events\n", (unsigned long long)(head - oldest));
	seq_printf(m, "total_written:  %llu\n", (unsigned long long)total);
	seq_printf(m, "dropped_frozen: %llu\n", (unsigned long long)dropped);
	seq_printf(m, "oldest_seq:     %llu\n", (unsigned long long)oldest);
	seq_printf(m, "head_seq:       %llu\n", (unsigned long long)head);
	seq_printf(m, "state:          %s\n", frozen ? "FROZEN" : "RECORDING");
	seq_printf(m, "heartbeat_ms:   %u\n", heartbeat_ms);
	return 0;
}

/* ---- device node permissions ------------------------------------------- */

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 2, 0)
static char *bb_devnode(const struct device *dev, umode_t *mode)
#else
static char *bb_devnode(struct device *dev, umode_t *mode)
#endif
{
	if (mode)
		*mode = 0666;   /* demo convenience: any user may log events */
	return NULL;
}

/* ---- module init / exit ------------------------------------------------ */

static int __init bb_init(void)
{
	int ret;

	if (buf_events < 16 || buf_events > (1U << 20)) {
		pr_err(DRV_NAME ": buf_events must be 16..1048576\n");
		return -EINVAL;
	}

	bb.capacity = buf_events;
	bb.events = kvcalloc(bb.capacity, sizeof(*bb.events), GFP_KERNEL);
	if (!bb.events)
		return -ENOMEM;

	spin_lock_init(&bb.lock);
	init_waitqueue_head(&bb.wq);
	timer_setup(&bb.hb_timer, bb_heartbeat, 0);

	ret = alloc_chrdev_region(&bb.devt, 0, 1, DRV_NAME);
	if (ret)
		goto err_free;

	cdev_init(&bb.cdev, &bb_fops);
	bb.cdev.owner = THIS_MODULE;
	ret = cdev_add(&bb.cdev, bb.devt, 1);
	if (ret)
		goto err_region;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
	bb.cls = class_create(DRV_NAME);
#else
	bb.cls = class_create(THIS_MODULE, DRV_NAME);
#endif
	if (IS_ERR(bb.cls)) {
		ret = PTR_ERR(bb.cls);
		goto err_cdev;
	}
	bb.cls->devnode = bb_devnode;

	bb.dev = device_create(bb.cls, NULL, bb.devt, NULL, DRV_NAME);
	if (IS_ERR(bb.dev)) {
		ret = PTR_ERR(bb.dev);
		goto err_class;
	}

	bb.proc = proc_create_single(DRV_NAME, 0444, NULL, bb_proc_show);
	if (!bb.proc) {
		ret = -ENOMEM;
		goto err_device;
	}

	if (heartbeat_ms)
		mod_timer(&bb.hb_timer, jiffies + msecs_to_jiffies(heartbeat_ms));

	pr_info(DRV_NAME ": loaded, major %d, %u events (%zu KiB), heartbeat %u ms\n",
		MAJOR(bb.devt), bb.capacity,
		(size_t)bb.capacity * sizeof(struct bb_event) / 1024, heartbeat_ms);
	return 0;

err_device:
	device_destroy(bb.cls, bb.devt);
err_class:
	class_destroy(bb.cls);
err_cdev:
	cdev_del(&bb.cdev);
err_region:
	unregister_chrdev_region(bb.devt, 1);
err_free:
	kvfree(bb.events);
	return ret;
}

static void __exit bb_exit(void)
{
	bb_timer_stop();
	remove_proc_entry(DRV_NAME, NULL);
	device_destroy(bb.cls, bb.devt);
	class_destroy(bb.cls);
	cdev_del(&bb.cdev);
	unregister_chrdev_region(bb.devt, 1);
	kvfree(bb.events);
	pr_info(DRV_NAME ": unloaded\n");
}

module_init(bb_init);
module_exit(bb_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("BlackBox Student");
MODULE_DESCRIPTION("BlackBox flight recorder - ring buffer char device");

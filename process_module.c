#include<linux/kernel.h>
#include<linux/init.h>
#include<linux/module.h>
#include<linux/kdev_t.h>
#include<linux/fs.h>
#include<linux/cdev.h>
#include<linux/device.h>
#include<linux/slab.h>
#include<linux/uaccess.h>
#include<linux/string.h>


//Size macro
#define DATA_SIZE 512

//Global pointer to memory region
static char *dataPtr;

//Initial values for the first and second command.
int PID = 1;
char *option = "-x";
struct pid *pid;
struct task_struct *task;
char *token;


//Getting input from the command line.
module_param(PID, int, 0);
module_param(option, charp, 0);


dev_t dev = 0;
static struct class *dev_class;
static struct cdev pst_cdev;

//Functions
static int __init process_driver_init(void);
static void __exit process_driver_exit(void);
static int pst_open(struct inode *inode, struct file *file);
static int pst_release(struct inode *inode, struct file *file);
static ssize_t pst_write(struct file *filp, const char *buf, size_t len, loff_t *off);
void dfs(struct task_struct *task);
void bfs(struct task_struct *task);

static struct file_operations fops = 
{
	.owner	= THIS_MODULE,
	.write	= pst_write,
	.open	= pst_open,
	.release = pst_release, 
};

static int pst_open(struct inode *inode, struct file * file)
{	

	//ALlocating a single block memory
	dataPtr = kzalloc(DATA_SIZE, GFP_KERNEL);
	
	//Error checking
	if(!dataPtr) {
	printk(KERN_INFO "Could not allocate memory!\n");
	}
	return 0;
}

static int pst_release(struct inode *inode, struct file *file)
{
	//Freeing the allocated memory region
	kfree(dataPtr);
	return 0;
}


static ssize_t pst_write(struct file *filp,const char __user *buf, size_t len, loff_t* off)
{

	//Copying content of the memory region from user to kernelş
	//Whenever user writes something that means s/he wants to run pstraverse
	//Resolve inputs and call pstraverse functions according to inputs
	if(copy_from_user(dataPtr,buf,len) != 0) {
		printk(KERN_INFO "Copying from user to kernel failed!\n");
	}

	token = strsep(&dataPtr, " ");

	if ((kstrtoint(token,10,&PID)))
		printk("Could not convert string to long int\n");

	token = strsep(&dataPtr, " ");	
	option = token;
	

	printk("PID: %d Option: %s",PID,option);
	pid = find_get_pid(PID);
	task = pid_task(pid, PIDTYPE_PID);

	printk(KERN_INFO "PID: %d Executable: %s\n", task->pid, task->comm);

	if(task == NULL) {
		printk("Could not find any process with process id: %d\n",PID);
		return 1;
	}

	if(strcmp(option, "-d") == 0) {

		dfs(task);
	}
	else if (strcmp(option, "-b") == 0) {

		bfs(task);
	}
	else {
		dfs(task);
	}
	
	return len;
}

/* Prints the process tree using dfs with ids and executable names
 * @param task_struct
 * */
void dfs(struct task_struct *task) {

	struct task_struct *task_next;
	struct list_head *list;


	list_for_each(list, &task->children) {

		task_next = list_entry(list, struct task_struct, sibling);
    		printk(KERN_INFO "PID: %d Executable: %s\n", task_next->pid, task_next->comm);
		dfs(task_next);
	}
}

/* Prints the process tree using bfs with ids and executable names
 * @param task_struct
 * */
void bfs(struct task_struct *task) {

	struct task_struct *task_next;
	struct list_head *list;
	struct list_head *list1;
	struct task_struct *task_next1;

		
	list_for_each(list, &task->children) {
			
		task_next = list_entry(list, struct task_struct, sibling);
		printk(KERN_INFO "PID: %d Executable: %s\n", task_next->pid, task_next->comm);

	}
	list_for_each(list1, &task->children) {	
		
		task_next1 = list_entry(list1, struct task_struct, sibling);
		bfs(task_next1);
		
	}
	
}

static int __init process_driver_init(void)
{
	
	if((alloc_chrdev_region(&dev, 0, 1, "pst_Dev")) < 0) {
		printk(KERN_INFO"Cannot allocate the major number...\n");
	}

	printk(KERN_INFO "Initializing pstraverse module.\n");
	printk("PID: %d options: %s\n", PID, option);	

	pid = find_get_pid(PID);
	task = pid_task(pid, PIDTYPE_PID);

	printk(KERN_INFO "PID: %d Executable: %s\n", task->pid, task->comm);

	if(task == NULL) {
		printk("Could not find any process with process id: %d\n",PID);
		return 1;
	}

	if(strcmp(option, "-d") == 0) {

		dfs(task);
	}
	else if (strcmp(option, "-b") == 0) {

		bfs(task);
	}
	else {
		dfs(task);
	}

	
	cdev_init(&pst_cdev, &fops);


	if((cdev_add(&pst_cdev, dev, 1)) < 0) {
		printk(KERN_INFO "Cannot add the device to the system...\n");
		goto r_class;
	}	 


	if((dev_class =  class_create(THIS_MODULE, "process_class")) == NULL) {
		printk(KERN_INFO " cannot create the struct class...\n");
		goto r_class;
	}



	if((device_create(dev_class, NULL, dev, NULL, "process_device")) == NULL) {
		printk(KERN_INFO " cannot create the device ..\n");
		goto r_device;
	}

	return 0;

r_device: 
	class_destroy(dev_class);

r_class:
	unregister_chrdev_region(dev, 1);
	return -1;
}

void __exit process_driver_exit(void) {
	device_destroy(dev_class, dev);
	class_destroy(dev_class);
	cdev_del(&pst_cdev);
	unregister_chrdev_region(dev, 1);
	printk(KERN_INFO "Exiting from pstraverse module.\n");
}

module_init(process_driver_init);
module_exit(process_driver_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Sinan Cem Erdoğan");
MODULE_DESCRIPTION("Pstraverse module");





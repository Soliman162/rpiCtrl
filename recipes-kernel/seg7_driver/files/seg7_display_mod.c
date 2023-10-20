#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/init.h>
#include <linux/stat.h>
#include <linux/ioctl.h>
#include <linux/uaccess.h>
#include <linux/gpio.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include "seg7_display_mod.h"

#define GET_BIT(reg,bit)	((reg>>bit)&1)

#define DEVICE_INDEX		0
#define NUMBER_INDEX		1

static int device_counter = 0;
static struct cdev seg7_struct ;
static struct class *device_class;
static struct device *device_struct;
static dev_t first_assigned_number = 0;
static struct gpio *seg7_pins[GPIO_PINS_NUMBER] = {NULL};
const unsigned char SSD_u8Numbers[10] = {0b00111111,0b00000110,0b01011011,0b01001111,0b01100110,0b01101101,0b01111101,0b00000111,0b01111111,0b01101111};


static struct of_device_id of_7_segment_match[] = 
{
	{ .compatible = "7_segment", },
	{}
};

MODULE_DEVICE_TABLE(of,of_7_segment_match);

static struct platform_driver segment7 = 
{
	.probe = module_probe,
	.shutdown = module_shutdown,
	.driver = {
		.name = "7segment-display",
		.owner = THIS_MODULE,
		.of_match_table = of_7_segment_match,
	},
};

static struct file_operations file_Op =
{
	.owner = THIS_MODULE,
	.open = open_module ,
	.write = write_module,
	.release = close_module
};


int open_module (struct inode *node, struct file *file_mod)
{
	printk("open 7 seg display driver\n");
	return 0;
}

ssize_t write_module(struct file *file_mod, const char __user *user_buffer, size_t count, loff_t *offs)
{
	int not_copied = 0;
	int seg7_index = 0;
	unsigned char seg7_num = 0;
	unsigned char seg7_pin = 0;
	int WRITE_SIZE = device_counter + 2 ;
	unsigned char write_buffer[WRITE_SIZE];
	
	if( *offs+count > WRITE_SIZE )
	{
		count = WRITE_SIZE - *offs;
	}
	if( !count )
	{
		return -1;
	}
	not_copied = copy_from_user(&write_buffer[*offs],user_buffer,count);
	if( not_copied )
	{
		return -1;
	}
	*offs = count;
	pr_info("data write to buffer\n");

	seg7_index = write_buffer[DEVICE_INDEX]-'0';

	if( 
		(write_buffer[NUMBER_INDEX]>='0') && 
		(write_buffer[NUMBER_INDEX]<='9') &&
		seg7_pins[seg7_index] != NULL
	  )
	{
		seg7_num = SSD_u8Numbers[write_buffer[NUMBER_INDEX]-'0'];
		for( seg7_pin=0;seg7_pin<GPIO_PINS_NUMBER;seg7_pin++ )
		{
			gpio_set_value(seg7_pins[seg7_index][seg7_pin].gpio,GET_BIT(seg7_num,seg7_pin)); 
		}
	}
	else
	{
		pr_err("invalid input\n");
		return -EIO;
	}

	return count;
}

int close_module (struct inode *node, struct file *file_mod)
{
	printk("close driver\n");
	return 0;
}

int module_probe(struct platform_device *pdev)
{
	int i = 0;
	int ret = 0;
	char *Pin_label;
	u8 Pins_array[GPIO_PINS_NUMBER] ;
	struct device *seg7 = &pdev->dev;
	struct device_node *Child_node_Iter = NULL;
	const struct device_node *parent_node = seg7->of_node;

	seg7_pins[device_counter] = (struct gpio *)kmalloc(sizeof(struct gpio *),GFP_KERNEL);
	if( *seg7_pins == NULL )
	{
		pr_err("Couldn't allocate memory\n");
		return -EHWPOISON;
	}
	if((Child_node_Iter = of_get_next_child(parent_node,Child_node_Iter)) == NULL)
	{
		ret = -EINVAL;
		pr_err("parent node is empty \n");
		goto GET_CHILD_ERROR; 
	}
	
	do
	{		
		pr_info("got child node %s\n",Child_node_Iter->name);
		/*read pins from property*/
		ret = of_property_read_u8_array(Child_node_Iter,"pins",Pins_array,GPIO_PINS_NUMBER);
		if( ret != 0 )
		{
			pr_err("error couldn't read array \n");
			goto GET_PROPERETY_ERROR;
		}
		/*set up pins*/
		for(i=0;i<GPIO_PINS_NUMBER;i++)
		{
			pr_info("start set pin %d\n",i);
			Pin_label = kasprintf(GFP_KERNEL,"pin%c",('A'+i));
			seg7_pins[device_counter][i] = (struct gpio){Pins_array[i],GPIOF_OUT_INIT_LOW,Pin_label};
			pr_info("finish set pin %d with label %s\n",i,Pin_label);
		}
		/*init pins*/
		ret = gpio_request_array(seg7_pins[device_counter],GPIO_PINS_NUMBER);
		if( ret != 0 )
		{
			pr_err("gpio request failed\n");
			goto GET_PROPERETY_ERROR;
		} 
		pr_info("Finish setting up the node %d pins\n",device_counter);
		device_counter++;
			/*check for another child node*/
		if( (Child_node_Iter = of_get_next_child(parent_node,Child_node_Iter)) != NULL )
		{
			/*reallocate size for new node pins*/
			*seg7_pins = krealloc(*seg7_pins,sizeof(struct gpio *)*(device_counter+1),GFP_KERNEL);
			if( *seg7_pins == NULL )
			{
				pr_err("Couldn't allocate memory for node %d\n",device_counter);
				of_node_put(Child_node_Iter);
				return -EHWPOISON;
			}
		}		
		
	}while(Child_node_Iter!=NULL);

	pr_info("Finish setting up the node pins\n");
	
	GET_PROPERETY_ERROR:
		of_node_put(Child_node_Iter);
	GET_CHILD_ERROR:
		kfree(seg7_pins[device_counter]);
		return ret;
}

void module_shutdown(struct platform_device *pdev)
{
	int i;
	for(i=0;i<device_counter;i++)
	{
		gpio_free_array(seg7_pins[i],GPIO_PINS_NUMBER);
	}
	for(i=device_counter-1;i>=0;i--)
	{
		kfree(seg7_pins[i]);
	}
}

static int __init seg_7display_init(void)
{
	int ret_value = 0;

	ret_value = __platform_driver_probe(&segment7,module_probe,THIS_MODULE);
	if( ret_value!=0 )
	{
		pr_err("failed to register platform device \n");
		return -1;
	}
	ret_value = alloc_chrdev_region(&first_assigned_number,0,(u8)GPIO_PINS_NUMBER,DEVICE_DRIVER_NAME);
	if(ret_value != 0)
	{
		pr_err("couldn't allocate number\n");
		goto ALLOC_CHRDEV_ERROR;
	}
	cdev_init(&seg7_struct,&file_Op);
	ret_value = cdev_add(&seg7_struct,first_assigned_number,GPIO_PINS_NUMBER);
	if(ret_value != 0)
	{
		pr_err("cdev add failed\n");
		goto DELETE_DEVICE_NUMBER;
	}
	if( (device_class = class_create(THIS_MODULE,CLASS_NAME)) == NULL )
	{
		pr_err("device class failed\n");
		goto DELETE_DEVICE_STRUCT;
	}
	device_struct = device_create(device_class,NULL,first_assigned_number,NULL,DEVICE_FILE_NAME);
	if( device_struct == NULL )
	{
		pr_err("device create failed\n");
		goto DELETE_CLASS;
	}

	return 0;

	DELETE_CLASS:
		class_destroy(device_class);
	DELETE_DEVICE_STRUCT:
		cdev_del(&seg7_struct);
	DELETE_DEVICE_NUMBER:
		unregister_chrdev_region(first_assigned_number,GPIO_PINS_NUMBER);
	ALLOC_CHRDEV_ERROR:
		platform_driver_unregister(&segment7);
		return -1;
}

static void __exit seg_7display_exit(void)
{   
	int seg7_pin = 0;
	platform_driver_unregister(&segment7);
	cdev_del(&seg7_struct);
	device_destroy(device_class,first_assigned_number);
	class_destroy(device_class);
	unregister_chrdev_region(first_assigned_number,GPIO_PINS_NUMBER);
	printk("stop 7seg\n");
}

module_init(seg_7display_init);
module_exit(seg_7display_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Soliman");
MODULE_DESCRIPTION("Simple device driver for 7 segment");
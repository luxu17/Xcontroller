#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/usb.h>

#define VID 0x20D6 // PowerA
#define PID 0x2035 // Xbox One Controller Wired Black

#define INT_BUF_SIZE 64 // Max packet size for the controller interrupt endpoints

// Tells the kernel what devices our driver supports
static struct usb_device_id usb_device_table[] = {{USB_DEVICE(VID, PID)}, {}};
MODULE_DEVICE_TABLE(usb, usb_device_table);

// Our device specific data structure. Also the
// URB context in our completion callback
struct usb_device_data {
	struct usb_device *udev; // Device
	struct urb *irq_urb_in;	 // URB in
	struct urb *irq_urb_out; // URB out
	unsigned char *irq_buffer_in;
	unsigned char *irq_buffer_out;
	dma_addr_t irq_dma_in;	// In DMA
	dma_addr_t irq_dma_out; // Out DMA
};

// Our controller input structure
struct XController_Input {

	// Pretty self-explanatory. The first two bytes are all of the binary positionable buttons.
	unsigned char xbox_btn : 1;
	unsigned char unknown1 : 1;
	unsigned char start_btn : 1;
	unsigned char select_btn : 1;
	unsigned char a_btn : 1;
	unsigned char b_btn : 1;
	unsigned char x_btn : 1;
	unsigned char y_btn : 1;

	unsigned char up_btn : 1;
	unsigned char down_btn : 1;
	unsigned char left_btn : 1;
	unsigned char up_right : 1;
	unsigned char left_bumper : 1;
	unsigned char right_bumper : 1;
	unsigned char unknown2 : 1;
	unsigned char unknown3 : 1;

	// The triggers are a bit odd. Still don't fully understand their behavior.
	// The first byte of both left/right is usually the only one used but occasionally they use the second byte and I have no clue why
	unsigned short left_trigger;
	unsigned short right_trigger;

	// Again, pretty self-explanatory.
	// I have been unsuccessful in identifying when a stick is pressed in. Seemingly none of the bits within the shorts
	// used to represent the sticks changes relative to it. It's even further complicated because when pressing the stick in
	// there's always gonna be slight movement. Trying to figure this out.
	short left_stick_vertical;
	short left_stick_horizontal;

	short right_stick_vertical;
	short right_stick_horizontal;

	// Byte 18 of the input data sent in the packet appears to be purely for extra functionality/buttons.
	unsigned char screen_capture_button : 1;
	unsigned char unknown4 : 7;

};

// Has to be sent to the controller first to begin receiving controller input.
static const unsigned char xbox_power_on[] = {

	0x05, 0x20, 0x00, 0x01, 0x00

};

// Found this on the SDL github
static const unsigned char xbox_init_powera_rumble[] = {

	0x09, 0x00, 0x00, 0x09, 0x00, 0x0F, 0x00, 0x00,

	0x1D, 0x1D, 0xFF, 0x00, 0x00

};

// Prints out the state of all inputs from the controller
void process_controller_input(struct usb_device_data *dev_data) {
    // Cast the relevant portion of the buffer to our struct
    struct XController_Input *input = (struct XController_Input *)(dev_data->irq_buffer_in + 4);

    // Print inputs
	// printk is not prefered, however I didn't learn this till later.
	// Will change instances of printk appropriately.
    printk(KERN_INFO "Xbox button state: %d\n", input->xbox_btn);
    printk(KERN_INFO "Start button state: %d\n", input->start_btn);
    printk(KERN_INFO "Select button state: %d\n", input->select_btn);

    printk(KERN_INFO "A button state: %d\n", input->a_btn);
    printk(KERN_INFO "B button state: %d\n", input->b_btn);
    printk(KERN_INFO "X button state: %d\n", input->x_btn);
    printk(KERN_INFO "Y button state: %d\n", input->y_btn);

    printk(KERN_INFO "Left Bumper state: %d\n", input->left_bumper);
    printk(KERN_INFO "Right Bumper state: %d\n", input->right_bumper);

    printk(KERN_INFO "Left Trigger state: %u\n", input->left_trigger);
    printk(KERN_INFO "Right Trigger state: %u\n", input->right_trigger);

    printk(KERN_INFO "Left stick horizontal: %d\n", le16_to_cpu(input->left_stick_horizontal));
    printk(KERN_INFO "Left stick vertical: %d\n", le16_to_cpu(input->left_stick_vertical));

    printk(KERN_INFO "Right stick horizontal: %d\n", le16_to_cpu(input->right_stick_horizontal));
    printk(KERN_INFO "Right stick vertical: %d\n", le16_to_cpu(input->right_stick_vertical));

}



// Completion callback that handles packet data from the controller
static void irq_completion_in(struct urb *urb)
{

	// I know static variables are a no-no in device drivers. This is purely in place while I learn.
	static bool first_pass = true;

	struct usb_device_data *dev_data = urb->context;

	static unsigned char prev_irq_buffer[INT_BUF_SIZE];
	int i;

	if (urb->status == 0) {
		// Used this to see differences in packet data sent to discern button presses and such.
		printk(KERN_INFO "Received data: %*phN\n", INT_BUF_SIZE,
		dev_data->irq_buffer_in);

		printk(KERN_INFO "Changes from the previous packet:\n");

		for (i = 0; i < INT_BUF_SIZE; i++) {
			if (dev_data->irq_buffer_in[i] != prev_irq_buffer[i]) {
				printk(KERN_INFO "Byte %d: Previous: 0x%02x, Current: 0x%02x\n", i, prev_irq_buffer[i], dev_data->irq_buffer_in[i]);
			}
		}

		// Update the previous buffer with the current buffer
		memcpy(prev_irq_buffer, dev_data->irq_buffer_in, INT_BUF_SIZE);

		process_controller_input(dev_data);

		// Resubmit the URB for continuous data
		// reception

		if(first_pass) {
			
			// Still trying to understand how PowerA rumble is initiated.
			memcpy(dev_data->irq_buffer_out, xbox_init_powera_rumble, sizeof(xbox_init_powera_rumble));

			printk(KERN_INFO "Sending xbox_init_powera_rumble packet to controller\n");

			int retval = usb_submit_urb(urb, GFP_ATOMIC);

			if (retval) {

				printk(KERN_ERR "Failed to submit rumble request: %d\n", retval);

			}

		} else {

			usb_submit_urb(urb, GFP_ATOMIC);

		}
	}
	else {

		printk(KERN_ERR "Interrupt URB IN error: %d\n", urb->status);

		if (urb->status != -ESHUTDOWN && urb->status != -ENOENT) {

			usb_submit_urb(urb, GFP_ATOMIC);

		}
	}

	first_pass = false;
}

// Completion out callback
static void irq_completion_out(struct urb *urb)
{

	if (urb->status != 0)
		printk(KERN_ERR "Interrupt URB OUT error: %d\n", urb->status);

	struct usb_device_data *dev_data = urb->context;

	printk(KERN_INFO "Data being sent to controller: %*phN\n", INT_BUF_SIZE,
		   dev_data->irq_buffer_out);
}

// Initializes various aspects of USB communication with the device
static int controller_probe(struct usb_interface *interface, const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(interface);
	struct usb_device_data *dev_data;
	struct usb_endpoint_descriptor *ep_desc_in;
	struct usb_endpoint_descriptor *ep_desc_out;
	int retval;

	if (interface->cur_altsetting->desc.bInterfaceNumber != 0) {
		return -ENODEV; // Only handle interface 0
	}

	if (usb_get_intfdata(interface)) {
		printk(KERN_INFO "Interface already initialized\n");
		return -EEXIST;
	}

	// Took me a minute to figure this out.
	// Was under the impression the USB data bus automatically handled power. I was wrong.
	pm_runtime_set_active(&interface->dev);
	pm_runtime_enable(&interface->dev);
	pm_runtime_get_noresume(&interface->dev);

	dev_data = kzalloc(sizeof(struct usb_device_data), GFP_KERNEL);
	if (!dev_data) {
		return -ENOMEM;
	}

	// Setting up DMA for our USB device driver. More efficient because it bypasses the CPU for Direct Memory Access (DMA)
	dev_data->udev = udev;
	dev_data->irq_buffer_in = usb_alloc_coherent(udev, INT_BUF_SIZE, GFP_KERNEL, &dev_data->irq_dma_in);
	if (!dev_data->irq_buffer_in) {
		retval = -ENOMEM;
		goto error;
	}

	// Allocates an empty URB
	dev_data->irq_urb_in = usb_alloc_urb(0, GFP_KERNEL);
	if (!dev_data->irq_urb_in) {
		retval = -ENOMEM;
		goto error_free_buffer_in;
	}

	dev_data->irq_buffer_out = usb_alloc_coherent(
		udev, INT_BUF_SIZE, GFP_KERNEL, &dev_data->irq_dma_out);
	if (!dev_data->irq_buffer_out) {
		retval = -ENOMEM;
		goto error_free_urb_in;
	}

	dev_data->irq_urb_out = usb_alloc_urb(0, GFP_KERNEL);
	if (!dev_data->irq_urb_out) {
		retval = -ENOMEM;
		goto error_free_buffer_out;
	}

	// Xbox controllers have two interrupt endpoints, IN and OUT.
	if (interface->cur_altsetting->desc.bNumEndpoints < 2) {
		retval = -ENODEV;
		goto error_free_urb_out;
	}

	ep_desc_in = &interface->cur_altsetting->endpoint[1].desc;
	ep_desc_out = &interface->cur_altsetting->endpoint[0].desc;

	if (!usb_endpoint_is_int_in(ep_desc_in)) {
		printk(KERN_ERR "IN endpoint is not an interrupt endpoint\n");
		retval = -EINVAL;
		goto error_free_urb_out;
	}

	if (!usb_endpoint_is_int_out(ep_desc_out)) {
		printk(KERN_ERR "OUT endpoint is not an interrupt endpoint\n");
		retval = -EINVAL;
		goto error_free_urb_out;
	}

	printk(KERN_INFO "Endpoint IN address: 0x%02x, Max packet size: %d, Interval: %d\n",
		   ep_desc_in->bEndpointAddress,
		   le16_to_cpu(ep_desc_in->wMaxPacketSize), ep_desc_in->bInterval);

	printk(KERN_INFO "Endpoint OUT address: 0x%02x, Max packet size: %d, Interval: %d\n",
		   ep_desc_out->bEndpointAddress,
		   le16_to_cpu(ep_desc_out->wMaxPacketSize), ep_desc_out->bInterval);

	// Fill our previously allocated, empty URB with pertinent data about our endpoint
	usb_fill_int_urb(dev_data->irq_urb_in, udev,
					 usb_rcvintpipe(udev, ep_desc_in->bEndpointAddress),
					 dev_data->irq_buffer_in,
					 le16_to_cpu(ep_desc_in->wMaxPacketSize), irq_completion_in,
					 dev_data, ep_desc_in->bInterval);

	dev_data->irq_urb_in->transfer_dma = dev_data->irq_dma_in;
	dev_data->irq_urb_in->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	// Submit our filled URB to the usbcore so it can be processed by the host controller
	retval = usb_submit_urb(dev_data->irq_urb_in, GFP_KERNEL);
	if (retval) {

		printk(KERN_ERR "Failed to submit interrupt URB IN: %d\n", retval);
		goto error_free_urb_out;

	}

	usb_fill_int_urb(dev_data->irq_urb_out, udev,
					 usb_sndintpipe(udev, ep_desc_out->bEndpointAddress),
					 dev_data->irq_buffer_out, INT_BUF_SIZE, irq_completion_out, dev_data,
					 ep_desc_out->bInterval);

	dev_data->irq_urb_out->transfer_dma = dev_data->irq_dma_out;
	dev_data->irq_urb_out->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	memcpy(dev_data->irq_buffer_out, xbox_power_on, sizeof(xbox_power_on));

	retval = usb_submit_urb(dev_data->irq_urb_out, GFP_KERNEL);
	if (retval) {

		printk(KERN_ERR "Failed to submit interrupt URB OUT: %d\n", retval);
		goto error_free_urb_out;

	}

	usb_set_intfdata(interface, dev_data);
	printk(KERN_INFO "Xbox Controller connected\n");

	if(retval) {

		printk(KERN_ERR "Failed to submit rumble request: %d\n", retval);
		goto error_free_urb_out;

	}

	return 0;

error_free_urb_out:
	usb_free_urb(dev_data->irq_urb_out);
error_free_buffer_out:
	usb_free_coherent(udev, INT_BUF_SIZE, dev_data->irq_buffer_out, dev_data->irq_dma_out);
error_free_urb_in:
	usb_free_urb(dev_data->irq_urb_in);
error_free_buffer_in:
	usb_free_coherent(udev, INT_BUF_SIZE, dev_data->irq_buffer_in, dev_data->irq_dma_in);
error:
	kfree(dev_data);
	return retval;
}

static void controller_disconnect(struct usb_interface *interface)
{
	struct usb_device_data *dev_data = usb_get_intfdata(interface);

	if (dev_data) {

		// Free relevant resources
		usb_kill_urb(dev_data->irq_urb_in);
		usb_kill_urb(dev_data->irq_urb_out);

		usb_free_urb(dev_data->irq_urb_in);
		usb_free_urb(dev_data->irq_urb_out);

		usb_free_coherent(dev_data->udev, INT_BUF_SIZE, dev_data->irq_buffer_in,dev_data->irq_dma_in);
		usb_free_coherent(dev_data->udev, INT_BUF_SIZE, dev_data->irq_buffer_out, dev_data->irq_dma_out);

		kfree(dev_data);

	}

	// Need to be called to decrement previous operations
	pm_runtime_put_noidle(&interface->dev);
	pm_runtime_disable(&interface->dev);

	usb_set_intfdata(interface, NULL);
	printk(KERN_INFO "Xbox Controller %04x:%04x disconnected\n", VID, PID);
}

static struct usb_driver controller_driver = {

	.name = "Xbox Controller driver",
	.id_table = usb_device_table,
	.probe = controller_probe,
	.disconnect = controller_disconnect,

};

static int __init controller_init(void)
{

	int result = usb_register(&controller_driver);

	if (result) {

		printk(KERN_INFO "USB registration failed\n");

	}

	return result;

}

static void __exit controller_exit(void) { usb_deregister(&controller_driver); }

module_init(controller_init);
module_exit(controller_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Anonymous");
MODULE_DESCRIPTION("Xbox Controller device driver");

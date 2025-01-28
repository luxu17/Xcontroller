For about three weeks I worked on a USB device driver in Linux for receiving input from an Xbox One Controller. I took a blackbox approach and/or going in blind with no documentation and not referencing any Github repositories that would have simplified this.

First Things First
I needed to get familiar with working with USB devices within Linux. I did this in a Kali VirtualBox. I had to learn about various useful functions in the command terminal. Such as lsub, dmesg, insmod, rmmod, and others.

lsusb - Lists currently connected USB devices and their Vendor ID and Product ID. More on this later.

dmesg - Outputs messages and event logging from the kernel ring buffer.

insmod - Allows me to load my own .ko file. And/or my own device drivers.

rmmod - Removes a previously loaded .ko file and/or device driver.

USB Core
Usbcore will call into a driver through callbacks defined in the driver structure and through the completion handler of URBs a driver submits. Only the former are in the scope of this document. These two kinds of callbacks are completely independent of each other. Information on the completion callback can be found in USB Request Block (URB).
- Kernel org docs

So the first thing was learning about how USB device drivers work in general.

Generally speaking they have a few key traits:

usb_device_id structure - This struct contains a list of Vendor and Product ID's that our device driver supports. This can be thought of as make and model of a car. But instead of something like Nissan Xterra. It's 20D6:2035 where 20D6 is the Vendor ID number and 2035 is the Product ID number. 20D6 is the manufacturer PowerA whom makes Xbox One Controllers. And 2035 is a specific controller they manufacturer "Xbox One Controller Wired Black".

MODULE_DEVICE_TABLE - will register our driver with the Usbcore for the devices we specified within our usb_device_id structure.

probe callback - A function in the USB driver that gets called to check if the driver can manage a specific USB interface. It initializes the device, allocates resources, and registers it with the USB core. Returns 0 if successful, or an error code otherwise such as -ENODEV.

disconnect callback - Gets called when a USB device is disconnected. It handles cleanup tasks, such as freeing resources, unregistering the device, and stopping any ongoing operations.

__init function - This typically calls usb_register which registers a USB driver with the USB core, making it available to handle USB devices that match the driver's device ID table.

__exit function - Calls usb_deregister which, you guessed it, deregisters our driver within the USB core.

MODULE_LICENSE - This is a necessity. When loading an unsigned kernel module you must set it to GPL. If not then the kernel will not load it because it assumes it's pirated.

And these are just the basics. If I went over everything needed to create USB device drivers this post would be very long (it already is).

Getting the controller to send input
This was confusing at first. Figuring this out consisted of some trial and error.

I created a function to receive data from the controllers interrupt endpoint. There are a few different types of endpoints for USB devices. There's control, bulk, interrupt, etc. Interrupt endpoints are useful for something like a controller because they're good for small, time-sensitive data such as input to a video game.

I created a function to discern the difference between the previous and current packets. It would print a message to dmesg (which is the kernel ring buffer) which included any bytes that had changed since the previous packet from the controllers interrupt endpoint. I was using this to see if certain bytes would change depending on if I was pressing a button. Nope. Nothing changed. Well shit.

So now, I needed to figure out if there was some sort of handshake that happens during the initial connection? There was. So I loaded a known good device driver using insmod xpad. Then I used Wireshark to analyze USB traffic. Low and behold it did have an initial packet that was sent to the controller before the controller began to send anything besides the same 64 bytes.

We now send it that packet which is 0x05, 0x20, 0x00, 0x01, 0x00. Once this packet was sent I suddenly started getting changes in the bytes depending on the buttons pressed. Great!

Reversing the input packet
The last part was essentially pressing buttons and figuring out the corresponding change in the packet we receive in response from the controllers interrupt endpoint. We needed to identify what bytes represented which inputs. I noticed that when pressing buttons like A, B, X, Y on the controller that only one byte was changing.

What does that mean? If for instance pressing A made the byte equal to 0x10, and B made it equal 0x20 but pressing them at the same time makes that byte equal to 0x30?

Well on the surface it would appear they're just added together. While this is the end result it isn't a good description of what's taking place. The buttons each corresponded to their own bit within that byte. A or 0x10 corresponds to 0001 0000 in binary. B or 0x20 corresponds to 0010 0000 in binary.

So if those bits are both set 0011 0000 that would be 0x30. Great! Now we understand that each button is represented via a single bit in this particular byte. With this, I was able to deduce all the button states within just two bytes. This included the Xbox Home Button, A, B, X, Y, bumpers, and the dpad.

What about triggers? Well I observed that when pulling the left trigger two bytes would change. When pulling the right trigger two other bytes would change. You'd think this would be represented by a 4 byte value like a float right? Nope. Device drivers in Linux avoid floats like the plague because of the performance overhead necessary. So instead these turned out to be unsigned shorts. Ranging from 0 up to 65535.

Then we had the sticks. Moving the left stick caused changes in 4 bytes. 2 bytes of which was for vertical input and the other 2 for horizontal input. Same thing for the right stick. These were signed shorts. That way it would be negative when changing from either left to right. Or from up to down.

Putting it altogether
Now that I knew what bytes represented which inputs I was able to create a structure to map onto the packet.

Conclusion
This was a lot of fun. I wanted to get into device driver programming and one of the few USB connectable devices I had was my Xbox Controller. So I decided to make a game out of it. With the end goal being to receive input from the controller without having to rely on any documentation from Microsoft, whom has a standard for GIP (Gaming Input Protocol) which defines a lot of stuff about this. Or having to rely on Github repositories such as XPad.

All-in-all I learned a lot about USB device drivers and was able to successfully reverse engineer the controllers input. Demystifying yet another aspect of computers for myself.

Now, I may or may not venture into use cases for it. Such as using it as a mouse device or something? Who knows. We'll see.

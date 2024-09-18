/*
Nico Villegas-Kirchman
8/19/24
*/


// These are headers for utilizing Linux-based libraries that help us enable USB Tethering on the RG and EXTs.
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/netdevice.h>

// These are the definitions for the USB class, subclass and Protocol for CDC Ethernet and RNDIS
#define CDC_COMMUNICATIONS_CLASS   0x02 //CDC Subclass
#define CDC_ETHERNET_SUBCLASS      0x06 //ECM Subclass
#define RNDIS_SUBCLASS             0x02  // For RNDIS subclass

// This creates a table of 2 objects that are of type usb_device_id. The way that this works is that usb_tethering_table is an array where the entires are of type usb_device_id.
static struct usb_device_id usb_tethering_table[] = { // USB_INTERFACE_INFO is a macro to match usb devices when they are plugged into a system.
    { USB_INTERFACE_INFO(CDC_COMMUNICATIONS_CLASS, CDC_ETHERNET_SUBCLASS, 0) },  // CDC Ethernet
    { USB_INTERFACE_INFO(CDC_COMMUNICATIONS_CLASS, RNDIS_SUBCLASS, 0) },        // RNDIS
    { } // Terminating entry since this is meant to run at a kernel level.
};
MODULE_DEVICE_TABLE(usb, usb_tethering_table); //Initializes and declares an object for this structure.

static struct usb_driver usb_tether_driver = { //Usb_driver is already declared in one of the linux modules so we are just creating an instance of the structure.
    .id_table = usb_tethering_table,
    .probe = usb_tether_probe,
    .disconnect = usb_tether_disconnect,
};

static int usb_tether_probe(struct usb_interface *interface, const struct usb_device_id *id) { //interface specifies the interface that is being probed and id contains information about device class, subclass, and protocol
    struct usb_host_interface *iface_desc; //Points to the current alternate setting of the device that is being probed
    struct usb_endpoint_descriptor *endpoint; //Used to store the descriptor for each endpoint in the current interface
    int i; //Loop counter to iterate over endpoints

    iface_desc = interface->cur_altsetting; //Stores the current alternate setting of the interface in iface_desc

    for (i = 0; i < iface_desc->desc.bNumEndpoints; i++) { //Iterates over endpoints defined in the current interface
        endpoint = &iface_desc->endpoint[i].desc;

        if (usb_endpoint_is_bulk_in(endpoint)) { //Determines whether the endpoint is a bulk in endpoint. Bulk in is for receiving data from the usb device to the host
            // Save bulk IN endpoint for receiving data from the device
            pr_info("Found bulk IN endpoint with address 0x%02x\n", endpoint->bEndpointAddress);
        } else if (usb_endpoint_is_bulk_out(endpoint)) { //Determines whether the endpoint is a bulk out endpoint. Bulk out is for sending data from the host to the usb device
            // Save bulk OUT endpoint for sending data to the device
            pr_info("Found bulk OUT endpoint with address 0x%02x\n", endpoint->bEndpointAddress);
        }
    }
    // Additional configuration may be required here, depending on the device.
    return 0;
}


static int setup_net_device(struct usb_tether_priv *priv) { //Priv is a pointer to a private data structure for the usb tethering driver
    struct net_device *netdev; //Pointer to the network device structure

    netdev = alloc_etherdev(sizeof(struct usb_tether_priv)); //Allocated and initialize data for an ethernet device
    if (!netdev)
        return -ENOMEM;

    // Save the network device in your private structure
    priv->netdev = netdev; //Newly creating device is now stored in a private structure for easily accessing the network device later

    // Set up network device fields (MAC address, operations, etc.)
    netdev->netdev_ops = &usb_tether_netdev_ops; //Points to the operation structure for the network where usb_tether... is used for the kernel to interact with the network device

    // Register the network device
    return register_netdev(netdev); //Registers the device with the kernel's networking subsystem
}


static int __init usb_tether_init(void) { //Registers USb Driver with the Kernel USB subsystem
    return usb_register(&usb_tether_driver);
}

static void __exit usb_tether_exit(void) { //Used for unregistering USB Driver and managing device cleanup
    usb_deregister(&usb_tether_driver);
}

module_init(usb_tether_init); //Tells the kernel that USB tether is the function to be called when the module is loaded. This is where USB drivers are registered.
module_exit(usb_tether_exit);//This is where the USB device is disconnected and cleaning occurs.

MODULE_LICENSE("GPL"); //License under which the module is distributed
MODULE_AUTHOR("Nicolas Villegas-Kirchman");
MODULE_DESCRIPTION("Custom USB Tethering Driver for devices where packages and OpenWRT version are outside of user control.");

static netdev_tx_t usb_tether_xmit(struct sk_buff *skb, struct net_device *netdev) { //skb is a socket buffer that holds the package to be transmitted. Netdev is the network interface for the device
    struct usb_tether_priv *priv = netdev_priv(netdev); //Retrieves private data
    skb->
    // Handle packet transmission
    // Submit the URB to send the data over the USB connection

    dev_kfree_skb(skb);  // Free the socket buffer after transmission
    return NETDEV_TX_OK;
}

static const struct net_device_ops usb_tether_netdev_ops = { //Defines operations for kernel to interact with device
    .ndo_start_xmit = usb_tether_xmit,
};

static void usb_tether_rx_complete(struct urb *urb) { //Handles the completion of USB data reception. Called after URB and data is transferred to the networking subsystem.
    struct usb_tether_priv *priv = urb->context; //Retrieves private data
    struct sk_buff *skb; // New socket buffer to hold data

    if (urb->status) {
        // Handle error
        return;
    }

    // Allocate a new socket buffer for the received data
    skb = netdev_alloc_skb(priv->netdev, urb->actual_length);
    if (!skb)
        return;

    // Copy received data to the socket buffer
    memcpy(skb_put(skb, urb->actual_length), urb->transfer_buffer, urb->actual_length);

    // Pass the packet to the network stack
    skb->protocol = eth_type_trans(skb, priv->netdev);
    netif_rx(skb);

    // Resubmit the URB to receive more data
    usb_submit_urb(urb, GFP_ATOMIC);
}

static void usb_tether_disconnect(struct usb_interface *interface) {
    struct usb_tether_priv *priv = usb_get_intfdata(interface); //Retrieves driver private data

    // Unregister the network device
    unregister_netdev(priv->netdev);

    // Free resources (URBs, memory, etc.)
    usb_free_urb(priv->rx_urb);
    usb_free_urb(priv->tx_urb);

    free_netdev(priv->netdev);

    pr_info("USB tethering device disconnected\n");
}

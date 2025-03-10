// Driver probe
#include "tf_luna.h"
#include <linux/completion.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/types.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/minmax.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/serdev.h>
#include <linux/types.h>
#include <linux/version.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 12, 0)
#include <asm/unaligned.h>
#else
#include <linux/unaligned.h>
#endif

// ========== Helper Functions ==========

/**
 * @brief Calculates a 8 bit checksum given a buffer
 *
 * @param buf Buffer to calculate checksum
 * @param len Length of buffer
 * @return Calculated 8 bit checksum
 */
static u8 calculate_checksum(const u8 *buf, size_t len)
{
    u8 checksum = 0;
    for (size_t i = 0; i < len; i++)
    {
        checksum += buf[i];
    }
    return checksum;
}

// ========== Serial Callbacks ==========

static int serdev_luna_receive_buf(struct serdev_device *serdev, const u8 *buf, size_t size)
{
    struct tf_luna_sensor *sensor = serdev_device_get_drvdata(serdev);
    if (sensor->driver_init)
    {
        // During device probe we sent some commands to the device, but ignore the response as serial callback isnt
        // registered yet The moment the probe function is done, the callback with the responses from the probing
        // process happens We ignore it, and consider the device "configured" on the first time the callback happens
        if (!sensor->configured && size >= 6)
        {
            sensor->configured = true;
            return size;
        }
        // Check if we are expecting a response/packet
        if (!sensor->frame.expected_length)
        {
            u16 response_header;

            // Wait for the header (2 bytes)
            if (size < 2)
                return 0;

            // Header received;
            response_header = get_unaligned_be16(buf);
            if (response_header != TF_LUNA_MEASUREMENT_HEADER)
                return 2;

            // If the measurement header is found, we are now expecting a response/packet
            sensor->frame.expected_length = 7;
            sensor->frame.length = 0;
            return 2;
        }

        // Add the current state of the buffer to the buffer
        size_t num;
        num = min(size, (size_t)(sensor->frame.expected_length - sensor->frame.length));
        memcpy(sensor->frame.data + sensor->frame.length, buf, num);
        sensor->frame.length += num;

        // A full response is completed
        if (sensor->frame.length == sensor->frame.expected_length)
        {
            // TODO: Check if frame is valiid, THEN perform completion
            tf_luna_serial_measurement *measurements = (tf_luna_serial_measurement *)sensor->frame.data;
            sensor->distance_raw = (measurements->Dist_H << 8) + measurements->Dist_L;
            sensor->signal_strength = (measurements->Amp_H << 8) + measurements->Amp_L;
            sensor->temperature_raw = (measurements->Temp_H << 8) + measurements->Temp_L;

            complete(&sensor->frame_ready);
            sensor->frame.expected_length = 0;
        }
        return num;
    }
    return 0;
}

// ========== Serial Communication Functions ==========

int send_serial_command(struct tf_luna_sensor *sensor, luna_cmd_id_t cmd_id, u8 *params, size_t params_len)
{
    u8 buf[TF_LUNA_MAX_SEND_BUFFER_SIZE];
    u8 checksum = 0;
    u8 cmd = cmd_id & 0xFF;
    u8 msg_len = 4 + params_len;
    int ret;
    pr_debug("Sending TF-Luna serial commands\n");
    // Some sanity checks to make sure the preallocated buffer is big enough
    if (params_len > sizeof(buf) - 4)
    {
        pr_err("TF-Luna: Parameter length too long\n");
        return -EINVAL;
    }

    buf[0] = TF_LUNA_COMMAND_HEADER; // Header
    buf[1] = msg_len;                // Total length (header + length byte + cmd id + params + checksum)
    buf[2] = cmd;                    // Cmd id

    // Copies command parameters
    if (params_len > 0 && params)
    {
        memcpy(&buf[3], params, params_len);
    }

    checksum = calculate_checksum(buf, msg_len - 1);

    buf[3 + params_len] = checksum;

    if (!sensor->serdev)
    {
        pr_err("serdev is NULL\n");
        return -ENODEV;
    }
    ret = serdev_device_write(sensor->serdev, buf, msg_len, TF_LUNA_TIMEOUT);

    if (ret < msg_len)
    {
        pr_err("Unable to send full command packet to the TF-Luna\n");
        return ret < 0 ? ret : -EIO;
    }

    return 0;
}

// ========== Linux serdev setup ==========

static struct serdev_device_ops luna_serdev_ops = {
    .receive_buf = serdev_luna_receive_buf,
    .write_wakeup = serdev_device_write_wakeup,
};

/**
 * @brief Linux serdev driver probe function. Sets up a serdev device
 *
 * @param serdev serdev device in kernel
 * @return Return status code
 */
static int tf_luna_serdev_probe(struct serdev_device *serdev)
{
    pr_info("Initializing the " DEVICE_NAME " sensor\n");

    struct iio_dev *indio_dev;
    struct tf_luna_sensor *sensor;
    int ret;

    // Allocate the IIO device
    indio_dev = devm_iio_device_alloc(&serdev->dev, sizeof(*sensor));
    if (!indio_dev)
    {
        pr_err("Failed to allocate IIO device for the " DEVICE_NAME "\n");
        return -ENOMEM;
    }

    sensor = iio_priv(indio_dev);
    serdev_device_set_drvdata(serdev, sensor);
    sensor->serdev = serdev;
    sensor->driver_init = false;
    sensor->baudrate = TF_LUNA_DEFAULT_BAUDRATE;

    // Sets up the serdev device
    pr_info("Initializing serdev interface for the " DEVICE_NAME "\n");
    serdev_device_set_client_ops(serdev, &luna_serdev_ops);
    ret = devm_serdev_device_open(&serdev->dev, serdev);
    if (ret)
    {
        dev_err(&serdev->dev, "Failed to open serdev device for the " DEVICE_NAME "\n");
        return ret;
    }

    // Sets serial specific things like baudrate
    serdev_device_set_baudrate(serdev, sensor->baudrate);
    serdev_device_set_flow_control(serdev, false);
    serdev_device_set_parity(serdev, SERDEV_PARITY_NONE);

    // Call the probe in tf_luna core to setup IIO and initialize the device
    ret = tf_luna_probe(indio_dev);
    if (ret)
    {
        dev_err(&serdev->dev, "Failed to initialize IIO device for the " DEVICE_NAME "\n");
        return ret;
    }
    // TODO: Maybe add a function to pass into devm_add_action_or_reset to turn off the sensor on driver removal?

    // Register the IIO device
    ret = devm_iio_device_register(&serdev->dev, indio_dev);

    if (ret)
    {
        dev_err(&serdev->dev, "Failed to register the " DEVICE_NAME " as an IIO device\n");
        iio_device_free(indio_dev);
        serdev_device_close(serdev);
        return ret;
    }

    // Initialization succesfull
    sensor->driver_init = true;
    pr_info(DEVICE_NAME " initialized succesfully\n");
    return 0;
}

static void tf_luna_serdev_remove(struct serdev_device *serdev)
{
    pr_info("Removing " DEVICE_NAME " serdev\n");
    // TODO: Do things here?
    pr_info("Removed " DEVICE_NAME " serdev\n");
}

MODULE_DEVICE_TABLE(of, tf_luna_of_match);

static struct serdev_device_driver tf_luna_serdev_driver = {
    .driver =
        {
            .name = SERDEV_DRIVER_NAME,
            .of_match_table = tf_luna_of_match,

        },
    .probe = tf_luna_serdev_probe,
    .remove = tf_luna_serdev_remove,
};

module_serdev_device_driver(tf_luna_serdev_driver);

MODULE_AUTHOR("Aeybel Varghese");
MODULE_DESCRIPTION("Benewake TF-Luna Lidar Sensor Serial Driver");
MODULE_LICENSE("GPL v2");

#include "tf_luna.h"
#include <linux/export.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/types.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/types.h>

static int send_command(struct tf_luna_sensor *sensor, luna_cmd_id_t cmd_id, u8 *params, size_t params_len)
{
    // TODO: Address this to be capable for i2c as well
    return send_serial_command(sensor, cmd_id, params, params_len);
}

static int set_sample_freq(struct tf_luna_sensor *sensor, u16 divisor)
{
    luna_cmd_id_t cmd_id = ID_SAMPLE_FREQ;

    u16 freq = 250;
    if (divisor == 0)
    {
        // Trigger Mode
        freq = 0;
    }
    else if (divisor < 2 || divisor > 500)
    {
        pr_err("Specified frequency divisor is out of the range [2,500] for the " DEVICE_NAME "\n");
        return -EINVAL;
    }
    else
    {
        freq = 500 / divisor;
    }

    u8 freq_lower = freq & 0xFF;
    u8 freq_upper = (freq >> 8) & 0xFF;
    u8 params[2] = {freq_lower, freq_upper};
    bool trigger_mode = (freq == 0);
    int ret;
    ret = send_command(sensor, cmd_id, params, 2);
    if (ret < 0)
    {
        pr_err("Failed to set TF-Luna sample frequency\n");
        return ret;
    }
    sensor->sampling_frequency = freq;
    sensor->trigger_mode = trigger_mode;
    sensor->sampling_divisor = divisor;
    return 0;
}

static int set_to_trigger_mode(struct tf_luna_sensor *sensor)
{
    return set_sample_freq(sensor, 0);
}

// Sensor IIO Data Channels
static const struct iio_chan_spec tf_luna_channels[] = {
    {
        .type = IIO_DISTANCE,
        .info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
    },
    {
        .type = IIO_INTENSITY,
        .info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
    },
    {
        .type = IIO_TEMP,
        .info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
    },
};

// IIO Callback when reading raw data from channel
static int tf_luna_read_raw(struct iio_dev *indio_dev, struct iio_chan_spec const *chan, int *val, int *val2, long mask)
{
    struct tf_luna_sensor *sensor = iio_priv(indio_dev);

    // Locks a mutex during a read
    mutex_lock(&sensor->lock);

    // TODO: Read the actual sensor readings
    switch (chan->type)
    {
    case IIO_DISTANCE:
        // Distance reading
        *val = sensor->distance_raw;
        break;
    case IIO_INTENSITY:
        // Signal strength reading
        *val = sensor->signal_strength;
        break;
    case IIO_TEMP:
        // Chip temperature reading
        *val = sensor->temperature_raw;
        break;
    default:
        mutex_unlock(&sensor->lock);
        return -EINVAL;
    }

    // Returns mutex
    mutex_unlock(&sensor->lock);
    return IIO_VAL_INT;
}

// IIO File ops
static const struct iio_info tf_luna_info = {
    .read_raw = tf_luna_read_raw,
};

// Driver probe
int tf_luna_probe(struct iio_dev *indio_dev)
{
    pr_info("Initializing IIO for the " DEVICE_NAME "\n");

    struct tf_luna_sensor *sensor = iio_priv(indio_dev);
    int ret;

    // Set up the IIO Device struct
    indio_dev->name = DRIVER_NAME;
    indio_dev->modes = INDIO_DIRECT_MODE;
    indio_dev->info = &tf_luna_info;
    indio_dev->channels = tf_luna_channels;
    indio_dev->num_channels = ARRAY_SIZE(tf_luna_channels);

    sensor->distance_raw = 0;
    sensor->signal_strength = 0;
    sensor->temperature_raw = 0;

    mutex_init(&sensor->lock);

    // Sets the device to trigger/poll mode
    ret = set_to_trigger_mode(sensor);
    if (ret)
    {
        pr_err("Failed to initialize TF-Luna in trigger mode\n");
        return ret;
    }
    sensor->trigger_mode = true;

    return 0;
}

EXPORT_SYMBOL_GPL(tf_luna_probe);

MODULE_AUTHOR("Aeybel Varghese");
MODULE_DESCRIPTION("Benewake TF-Luna Lidar Sensor Driver");
MODULE_LICENSE("GPL v2");

#ifndef __TF_LUNA
#define __TF_LUNA

#include <linux/completion.h>
#include <linux/iio/iio.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/serdev.h>
#include <linux/types.h>

#define TF_LUNA_COMMAND_HEADER 0x5A
#define TF_LUNA_MEASUREMENT_HEADER 0x5959
#define TF_LUNA_MAX_SEND_BUFFER_SIZE 32
#define TF_LUNA_TIMEOUT msecs_to_jiffies(10000)

#define DEVICE_NAME "TF-Luna"
#define DRIVER_NAME "TF-Luna Driver"
#define SERDEV_DRIVER_NAME "tf_luna-serdev"
#define TF_LUNA_DEFAULT_BAUDRATE 115200

typedef struct tf_luna_serial_measurement
{
    u8 Dist_L;
    u8 Dist_H;
    u8 Amp_L;
    u8 Amp_H;
    u8 Temp_L;
    u8 Temp_H;
    u8 Checksum;
} tf_luna_serial_measurement;

struct tf_luna_frame
{
    u8 data[TF_LUNA_MAX_SEND_BUFFER_SIZE];
    u16 expected_length;
    u16 length;
};

// Device Information
struct tf_luna_sensor
{
    struct serdev_device *serdev;
    struct mutex lock;
    bool driver_init;
    struct tf_luna_frame frame;
    struct completion frame_ready;
    bool configured;
    int baudrate;
    bool trigger_mode;
    int sampling_divisor;
    int sampling_frequency;
    int distance_raw;
    int distance_cm;
    int distance_mm;
    int signal_strength;
    int temperature_raw;
    int temperature_c;
    int temperature_f;
};

// Device commands

typedef enum
{
    ID_GET_VERSION = 0x01,
    ID_SOFT_RESET,
    ID_SAMPLE_FREQ,
    ID_SAMPLE_TRIG,
} luna_cmd_id_t;

int tf_luna_probe(struct iio_dev *indio_dev);
int send_serial_command(struct tf_luna_sensor *sensor, luna_cmd_id_t cmd_id, u8 *params, size_t params_len);

static const struct of_device_id tf_luna_of_match[] = {{.compatible = "benewake,tf-luna"}, {}};
#endif // !__TF_LUNA
#define __TF_LUNA

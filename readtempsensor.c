/*
Based on DHTXXD.c by http://abyz.me.uk/rpi/pigpio/
Code to read the DHTXX temperature/humidity sensors.
2016-02-16 Public Domain
*/

#include <stdio.h>
#include <stdlib.h>
#include <pigpio.h>

struct DHTXXD_s;

typedef struct DHTXXD_s DHTXXD_t;

#define DHT_GOOD         0
#define DHT_BAD_CHECKSUM 1
#define DHT_BAD_DATA     2
#define DHT_TIMEOUT      3

typedef struct
{
    int status;
    float temperature, humidity;
    double timestamp;
} DHTXXD_data_t;

struct DHTXXD_s
{
    int _cb_id, _in_code, _bits, _ready, _new_reading, _is_init;
    union
    {
        uint8_t _byte[8];
        uint64_t _code;
    };
    DHTXXD_data_t _data;
    uint32_t _last_edge_tick;
};

static void _decode_dhtxx(DHTXXD_t *self)
{
    uint8_t chksum;
    float div, t, h;
    int valid;

    self->_data.timestamp = time_time();

    chksum = (self->_byte[1] + self->_byte[2] +
            self->_byte[3] + self->_byte[4]) & 0xFF;

    valid = 0;

    if (chksum == self->_byte[0])
    {
        valid = 1;

        h = ((float)((self->_byte[4]<<8) + self->_byte[3]))/10.0;

        if (h > 110.0) valid = 0;

        if (self->_byte[2] & 128) div = -10.0; else div = 10.0;

        t = ((float)(((self->_byte[2]&127)<<8) + self->_byte[1])) / div;

        if ((t < -50.0) || (t > 135.0)) valid = 0;


        if (valid)
        {
            self->_data.temperature = t;
            self->_data.humidity = h;
            self->_data.status = DHT_GOOD;
        }
        else
        {
            self->_data.status = DHT_BAD_DATA;
        }
    }
    else
    {
        self->_data.status = DHT_BAD_CHECKSUM;
    }

    self->_ready       = 1;
    self->_new_reading = 1;
}

static void _cb(int gpio, int level, uint32_t tick, void *user)
{
    DHTXXD_t *self=user;
    int edge_len;

    /* only rising edges will be processed */
    if (level != 1) return;

    edge_len = tick - self->_last_edge_tick;
    self->_last_edge_tick = tick;

    if (self->_is_init)
    {
        self->_is_init = 0;
        self->_in_code = 1;
        self->_bits = -2;
        self->_code = 0;
    }
    else if (self->_in_code)
    {
        self->_bits++;
        if (self->_bits >= 1)
        {
            self->_code <<= 1;

            if ((edge_len >= 60) && (edge_len <= 100))
            {
                /* 0 bit */
            }
            else if ((edge_len > 100) && (edge_len <= 150))
            {
                /* 1 bit */
                self->_code += 1;
            }
            else
            {
                /* invalid bit */
                self->_in_code = 0;
            }

            if (self->_in_code)
            {
                if (self->_bits == 40)
                {
                    _decode_dhtxx(self);
                }
            }
        }
    }
}

int main(int argc, char *argv[])
{
    int       i;
    DHTXXD_t* dht;
    double    timestamp;

    int outPin = 4;

#ifdef TEMPSENSORGPIO
    outPin = atoi ( TEMPSENSORGPIO ); // if GPIO pin was given by preprocessor use that one
#endif

    // init pigpio
    if (gpioInitialise() < 0)
    {
        // Initialization failed
        printf("GPIO Initialization failed\n");
        return 1;
    }

    /* create DHTXX */
    dht = malloc(sizeof(DHTXXD_t));

    if (!dht) return 1;

    dht->_is_init          = 1;
    dht->_data.status      = 0;
    dht->_data.temperature = 0.0;
    dht->_data.humidity    = 0.0;
    dht->_in_code          = 0;
    dht->_ready            = 0;
    dht->_new_reading      = 0;

    gpioSetMode(outPin, PI_INPUT);

    gpioSetAlertFuncEx(outPin, _cb, dht);

    /* read sensor, trigger measurement */
    gpioWrite(outPin, 0);
    time_sleep(0.001);
    gpioSetMode(outPin, PI_INPUT);

    timestamp = time_time();

    /* timeout if no new reading */
    for (i=0; i<5; i++) /* 0.25 seconds */
    {
        time_sleep(0.05);
        if (dht->_new_reading) break;
    }

    if (!dht->_new_reading)
    {
        dht->_data.timestamp = timestamp;
        dht->_data.status    = DHT_TIMEOUT;
        dht->_ready          = 1;
    }

    // output measurement result on stdout
    if (!dht->_data.status)
    {
        printf("%.1f %.1f\n", dht->_data.temperature, dht->_data.humidity);
    }

    // cleanup
    gpioTerminate();
    if (dht) free(dht);

    return 0;
}

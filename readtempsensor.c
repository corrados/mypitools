/*
Based on DHTXXD.c by http://abyz.me.uk/rpi/pigpio/
Code to read the DHTXX temperature/humidity sensors.
2016-02-16 Public Domain
*/

#include <stdio.h>
#include <stdlib.h>
#include <pigpiod_if2.h>

struct DHTXXD_s;

typedef struct DHTXXD_s DHTXXD_t;

#define DHT_GOOD         0
#define DHT_BAD_CHECKSUM 1
#define DHT_BAD_DATA     2
#define DHT_TIMEOUT      3

typedef struct
{
   int pi, status;
   float temperature, humidity;
   double timestamp;
} DHTXXD_data_t;

struct DHTXXD_s
{
   int pi, seconds, _cb_id, _in_code, _bits, _ready, _new_reading, _ignore_reading;
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

static void _cb(int pi, unsigned gpio, unsigned level, uint32_t tick, void *user)
{
   DHTXXD_t *self=user;
   int edge_len;

   edge_len = tick - self->_last_edge_tick;
   self->_last_edge_tick = tick;

   if (edge_len > 10000)
   {
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
               if (!self->_ignore_reading) _decode_dhtxx(self);
            }
         }
      }
   }
}

int main(int argc, char *argv[])
{
   int       pi, i;
   DHTXXD_t* dht;
   double    timestamp;

   int outPin = 4;

#ifdef TEMPSENSORGPIO
outPin = atoi ( TEMPSENSORGPIO ); // if GPIO pin was given by preprocessor use that one
#endif

   pi = pigpio_start(NULL, NULL); /* Connect to local Pi. */

   if (pi >= 0)
   {
	  /* create DHTXX */
      dht = malloc(sizeof(DHTXXD_t));
      
      if (!dht) return 1;
      
      dht->pi                = pi;
      dht->seconds           = 0;
      dht->_data.pi          = pi;
      dht->_data.status      = 0;
      dht->_data.temperature = 0.0;
      dht->_data.humidity    = 0.0;
      dht->_ignore_reading   = 0;
      dht->_in_code          = 0;
      dht->_ready            = 0;
      dht->_new_reading      = 0;
      
      set_mode(pi, outPin, PI_INPUT);
      
      dht->_last_edge_tick = get_current_tick(pi) - 10000;
      
      dht->_cb_id = callback_ex(pi, outPin, RISING_EDGE, _cb, dht);

      /* read sensor, trigger measurement */
      gpio_write(dht->pi, outPin, 0);
      time_sleep(0.001);
      set_mode(dht->pi, outPin, PI_INPUT);
      
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

// TEST
printf("%d %.1f %.1f\n", dht->_data.status, dht->_data.temperature, dht->_data.humidity);

      /* cancel DHTXX */
      if (dht)
      {
         if (dht->_cb_id >= 0) callback_cancel(dht->_cb_id);

         free(dht);
      }	  

      pigpio_stop(pi); /* Disconnect from local Pi. */
   }

   return 0;
}

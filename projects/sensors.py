import RPi.GPIO as GPIO
import time
 
GPIO_TRIG = 18
GPIO_ECHO = 17

GPIO.setmode(GPIO.BCM)
GPIO.setup(GPIO_TRIG, GPIO.OUT)
GPIO.setup(GPIO_ECHO, GPIO.IN)
 
def measdist():
    GPIO.output(GPIO_TRIG, True)
    time.sleep(0.00001)
    GPIO.output(GPIO_TRIG, False)
 
    t_start = time.time()
    t_stop  = time.time()
    while GPIO.input(GPIO_ECHO) == 0:
        t_start = time.time()
    while GPIO.input(GPIO_ECHO) == 1:
        t_stop = time.time()
 
    return ((t_stop - t_start) * 34300) / 2

def sensorscleanup():
    GPIO.cleanup()


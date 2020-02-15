import RPi.GPIO as GPIO
import time
import subprocess
 
GPIO_TRIG = 18
GPIO_ECHO = 17
 
GPIO.setmode(GPIO.BCM)
GPIO.setup(GPIO_TRIG, GPIO.OUT)
GPIO.setup(GPIO_ECHO, GPIO.IN)
 
def dist():
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
 
if __name__ == '__main__':
    try:
        while True:
            meashdist = dist()
            if meashdist < 10:
                p = subprocess.Popen(["./legoremote", "1R_BRAKE_1B_BRAKE"])
                subprocess.Popen.wait(p)
            else:
                p = subprocess.Popen(["./legoremote", "1R_3_1B_3"])
                subprocess.Popen.wait(p)

            print ("Measured Distance %.1f cm" % meashdist)
            time.sleep(0.5)
 
    except KeyboardInterrupt:
        print("Abort")
        GPIO.cleanup()

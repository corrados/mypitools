import time
import subprocess
from sensors import measdist
from sensors import sensorscleanup

def simple_lego_car_control():
    meashdist = measdist()
    if meashdist < 20:
        # stop both motors
        p = subprocess.Popen(["./legoremote", "1R_BRAKE_1B_BRAKE"])
        subprocess.Popen.wait(p)

        # backup a bit
        p = subprocess.Popen(["./legoremote", "1B_M3"])
        subprocess.Popen.wait(p)
        p = subprocess.Popen(["./legoremote", "1R_3"])
        subprocess.Popen.wait(p)
        time.sleep(1)

        # turn a bit
        p = subprocess.Popen(["./legoremote", "1R_M4"])
        subprocess.Popen.wait(p)
        time.sleep(0.5)
        p = subprocess.Popen(["./legoremote", "1R_BRAKE"])
        subprocess.Popen.wait(p)
    else:
        p = subprocess.Popen(["./legoremote", "1B_3"])
        subprocess.Popen.wait(p)
        p = subprocess.Popen(["./legoremote", "1R_M3"])
        subprocess.Popen.wait(p)

    print ("Measured Distance %.1f cm" % meashdist)

def meas_dist_testing():
    meashdist = measdist()
    print("Measured Distance %.1f cm" % meashdist)
    #time.sleep(0.2)

if __name__ == '__main__':
    try:
        while True:
            simple_lego_car_control()
            #meas_dist_testing()
 
    except KeyboardInterrupt:
        print("Abort")
        sensorscleanup()


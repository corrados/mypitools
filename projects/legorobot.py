import time
import subprocess
import statistics
from sensors import measdist
from sensors import sensorscleanup

def simple_lego_car_control():
    meashdist = measdist(); print ("Measured Distance %.1f cm" % meashdist)
    if meashdist < 20:
        # stop both motors
        p = subprocess.Popen(["./legoremote", "1R_BRAKE_1B_BRAKE"]); subprocess.Popen.wait(p)
        # backup a bit
        p = subprocess.Popen(["./legoremote", "1B_M3"]); subprocess.Popen.wait(p)
        p = subprocess.Popen(["./legoremote", "1R_3"]); subprocess.Popen.wait(p)
        time.sleep(1)
        # turn a bit
        p = subprocess.Popen(["./legoremote", "1R_M4"]); subprocess.Popen.wait(p)
        time.sleep(0.5)
        p = subprocess.Popen(["./legoremote", "1R_BRAKE"]); subprocess.Popen.wait(p)
    else:
        p = subprocess.Popen(["./legoremote", "1B_3"]); subprocess.Popen.wait(p)
        p = subprocess.Popen(["./legoremote", "1R_M3"]); subprocess.Popen.wait(p)

def meas_dist_testing():
    # collect multiple measurements and calculate statistics
    x = [0] * 30
    for i in range(0, len(x)):
        x[i] = measdist()
    print("Mean %.1f cm, stdev %.1f cm" % (statistics.mean(x), statistics.stdev(x)))

if __name__ == '__main__':
    try:
        while True:
            simple_lego_car_control()
            #meas_dist_testing()
 
    except KeyboardInterrupt:
        print("Abort")
        sensorscleanup()


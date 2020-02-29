import time
import subprocess
import statistics
from sensors import measdist
from sensors import sensorscleanup

def measdist_stat(): # collect multiple measurements and calculate statistics
    x = [0] * 20 # specify the statistics length here
    for i in range(0, len(x)): x[i] = measdist()
    return (statistics.mean(x), statistics.stdev(x))

def simple_lego_car_control():
    meashdist, meashstdev = measdist_stat(); print ("Distance %.1f cm, stdev %.1f cm" % (meashdist, meashstdev))
    if meashdist < 20: # 20 cm is the limit to the obstacle (we need some time to react)
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

if __name__ == '__main__':
    try:
        while True:
            simple_lego_car_control()
 
    except KeyboardInterrupt:
        print("Abort")
        sensorscleanup()

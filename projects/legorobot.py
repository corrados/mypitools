import time
import subprocess
from sensors import measdist
from sensors import sensorscleanup
 
if __name__ == '__main__':
    try:
        while True:
            meashdist = measdist()
            if meashdist < 20:
                # stop both motors
                p = subprocess.Popen(["./legoremote", "1R_BRAKE_1B_BRAKE"])
                subprocess.Popen.wait(p)

                # backup a bit
                p = subprocess.Popen(["./legoremote", "1R_M4_1B_4"])
                subprocess.Popen.wait(p)
                time.sleep(1)

                # turn a bit
                p = subprocess.Popen(["./legoremote", "1R_4"])
                subprocess.Popen.wait(p)
                p = subprocess.Popen(["./legoremote", "1R_BRAKE"])
                subprocess.Popen.wait(p)
            else:
                p = subprocess.Popen(["./legoremote", "1R_4_1B_M4"])
                subprocess.Popen.wait(p)

            print ("Measured Distance %.1f cm" % meashdist)
            time.sleep(0.5)
 
    except KeyboardInterrupt:
        print("Abort")
        sensorscleanup()


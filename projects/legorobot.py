import time
import subprocess
from sensors import measdist
from sensors import sensorscleanup
 
if __name__ == '__main__':
    try:
        while True:
            meashdist = measdist()
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
        sensorscleanup()


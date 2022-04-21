import os
import sys
import random

rootdir = "/home/francesco/Scrivania/Tesi/Clustering" #Path della repo "Clustering" sul dispositivo

applications = {"KCC"   : ["NR_DPUS=X NR_TASKLETS=Y BL=Z TYPE=UINT32 make -B all", "./bin/host_app -n #points -k #centers -d #dimensions"]}

def run(app_name):

    if (app_name not in applications):
        print("Application: '" + app_name + "' not avaible.")
        print("Avaible applications:")
        for key, _ in applications.items():
            print(key)
        return

    print ("------------------------ Running: "+app_name+" ----------------------")
    print ("--------------------------------------------------------------------")
        
    NR_DPUS = [1, 4, 16, 64]
    NR_TASKLETS = [1, 2, 4, 8, 16]
    BLOCK_LENGTH = ["1024", "512", "256", "128", "64"]

    make_cmd = applications[app_name][0]
    run_cmd = applications[app_name][1]

    os.chdir(rootdir + "/" + app_name)
    
    os.system("make clean")
    
    os.mkdir(rootdir + "/" + app_name + "/bin")
    os.mkdir(rootdir + "/" + app_name + "/profile")


    for d in NR_DPUS:
        for i, t in enumerate(NR_TASKLETS):
            m = make_cmd.replace("X", str(d))
            m = m.replace("Y", str(t))
            m = m.replace("Z", BLOCK_LENGTH[i])
            
            os.system(m)

            if (d == 1 or d == 4):
                n_points =  random.randint(1000, 2000)
                n_centers = random.randint(5, 15)
                n_dimensions = random.randint(1,5)
            else:
                n_points =  random.randint(1000, 10000)
                n_centers = random.randint(5, 15)
                n_dimensions = random.randint(1,5)

            r_cmd = run_cmd.replace("#points", str(n_points))
            r_cmd = r_cmd.replace("#centers", str(n_centers))
            r_cmd = r_cmd.replace("#dimensions", str(n_dimensions))
            
            file_name = rootdir + "/" + app_name + "/profile/results_dpu"+str(d)+"_tl"+str(t)+".txt"

            f = open(file_name, "a")
            f.write("Allocated " + str(d) + " DPU(s)\n")
            f.write("NR_TASKLETS " + str(d) + " BLOCK_LENGTH " + BLOCK_LENGTH[i] + "\n")
            f.write("Params: -n " + str(n_points) + " -k " + str(n_centers) + " -d " + str(n_dimensions) + "\n\n")
            f.close()

            print("Running: " + r_cmd)
            r_cmd = r_cmd + " >> " + file_name
            os.system(r_cmd)


def main():
    if (len(sys.argv) < 2):
        print ("Usage: python3 " + sys.argv[0] + " 'application_name'")
        print ("Applications avaiable:")
        for key, _ in applications.items():
            print(key)
        return 

    app = sys.argv[1]
    run(app)

if __name__ == "__main__":
    main()
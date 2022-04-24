#!/bin/sh
#compile everything
#compile user code
gcc -O3 -Wall -std=c11 message_sender.c -o sender
gcc -O3 -Wall -std=c11 message_reader.c -o reader
#compile kernel module
make clean
make all
#remove kernel module and devices
sudo rm /dev/slot0
sudo rmmod message_slot
#add kernel module and new device
sudo insmod message_slot.ko

#Tester2
sudo mknod /dev/slot0 c 235 0
sudo chmod 777 /dev/slot0
#run sender
./sender /dev/slot0 1 fuck 
./reader /dev/slot0 1

#Tester2
sudo mknod /dev/slot1 c 235 1
sudo chmod 777 /dev/slot1
sudo mknod /dev/slot2 c 235 2
sudo chmod 777 /dev/slot2
./sender /dev/slot1 1 inChannel1Dev1
./sender /dev/slot2 1 inChannel2Dev2
echo inChannel1Dev1=
./reader /dev/slot1 1
echo inChannel1Dev2=
./reader /dev/slot2 1

#Tester3
sudo mknod /dev/slot3 c 235 3
sudo chmod 777 /dev/slot3
./sender /dev/slot3 1 inChannel1Dev3
./sender /dev/slot3 2 inChannel2Dev3
echo inChannel1Dev3=
./reader /dev/slot3 1
echo inChannel2Dev3=
./reader /dev/slot3 2

#Tester4
./sender /dev/slot1 4 inChannel4Dev1_msg2
./sender /dev/slot3 1 inChannel1Dev3_msg2
echo inChannel4Dev1_msg2=
./reader /dev/slot3 4
echo inChannel1Dev3_msg2=
./reader /dev/slot3 1

#Tester5
#check ioctl!

#Tester6
sudo mknod /dev/slot6 c 235 3
sudo chmod 777 /dev/slot6
./sender /dev/slot6 1 inChannel1Dev6_try1
./sender /dev/slot6 1 inChannel1Dev6_try2
echo inChannel1Dev6_try2=
./reader /dev/slot6 1
echo inChannel1Dev6_try2=
./reader /dev/slot6 1

#Tester7
#try utf-8

#Tester8
echo prepareError
./reader /dev/slot6 2

#Tester9
./sender /dev/slot6 1 kerjfhkfjhekfjherkfhekrfhkerfjherkfjherkfherkfjehrkfjhekfherkfjherfkjerhfkjfhkejfhkerjfhekrfjerkfjhekrfjherkfjherkfjherkfhkjerfhkejrfhk

#Tester10
./sender /dev/slot6 1048577 bla








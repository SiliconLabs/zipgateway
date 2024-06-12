import socket
import subprocess

#UDP_IP = "127.0.0.1"
UDP_IP = "192.168.81.159"
UDP_PORT = 50005

sock = socket.socket(socket.AF_INET,  # Internet
                     socket.SOCK_DGRAM)  # UDP
sock.bind((UDP_IP, UDP_PORT))

while True:
    data, addr = sock.recvfrom(1024)  # buffer size is 1024 bytes
    # print "received message:", data.encode('hex')
    if data == "Sam says \'Reboot!\'":
        print('power-cycling ziprouter')
        subprocess.call(['sudo', './kill_ziprouter'])
    else:
        print('ignoring unknown payload')

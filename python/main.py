import math
import socket
import time
import math

if __name__ == '__main__':
    start_time = time.time()
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)

    # listen to this port
    s.bind(('', 4211))

    s.settimeout(1)
    s.setblocking(False)

    while True:
        tick = (time.time() - start_time) * 1000    # tick in ms

        if tick % 5000 == 0:
            print("sync broadcast send")
            s.sendto(math.ceil(tick).to_bytes(4, byteorder='little'), ('<broadcast>', 4210))

        try:
            data, addr = s.recvfrom(128)
            t = int.from_bytes(data, byteorder='little')
            print(tick, "tick from", addr, t)
        except socket.error:
            pass  # no data yet

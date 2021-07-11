import math
import socket
import time
import math

clients = {}

if __name__ == '__main__':
    start_time = time.time()
    udp_sync_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    udp_sync_socket.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)

    # listen to this port
    udp_sync_socket.bind(('', 4210))
    udp_sync_socket.settimeout(1)
    udp_sync_socket.setblocking(False)

    while True:
        tick = (time.time() - start_time) * 1000    # tick in ms

        if tick % 5000 == 0:
            print("sync broadcast send")
            udp_sync_socket.sendto(math.ceil(tick).to_bytes(4, byteorder='little'), ('<broadcast>', 4210))

        try:
            data, addr = udp_sync_socket.recvfrom(128)
            print(tick, "tick from", addr, int.from_bytes(data, byteorder='little'))

            if addr[0] != '192.168.0.247' and addr not in clients:
                clients[addr] = {'socket': None}
        except socket.error:
            pass  # no data yet

        for addr in clients:
            if clients[addr]['socket'] is None:
                print("open TCP socket")
                clients[addr]['socket'] = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                clients[addr]['socket'].settimeout(1)
                clients[addr]['socket'].connect((addr[0], 9999))
                clients[addr]['socket'].setblocking(False)
            else:
                try:
                    data = clients[addr]['socket'].recv(128)
                    print(tick, "BUZZERED", addr, int.from_bytes(data, byteorder='little'))
                except socket.error:
                    pass  # no data yet

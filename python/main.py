import math
import socket
import time
import math
import select

clients = []


def get_client(**kwargs):
    if len(clients) < 1:
        return None
    for key, value in kwargs.items():
        for c in clients:
            if c[key] == value:
                return c
    return None


def reset_buzzer():
    for c in clients:
        c['socket'].sendall(bytearray([0x10, 0xFF]))


if __name__ == '__main__':
    start_time = time.time()
    udp_sync_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    udp_sync_socket.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)

    # listen to this port
    udp_sync_socket.bind(('', 4210))
    udp_sync_socket.settimeout(1)
    udp_sync_socket.setblocking(False)

    last_sync = 0
    last_check_alive = 0

    while True:
        tick = (time.time() - start_time) * 1000  # tick in ms

        if time.time() - last_sync > 5.0:
            last_sync = time.time()
            print("sync broadcast send")
            udp_sync_socket.sendto(math.ceil(tick).to_bytes(4, byteorder='little'), ('<broadcast>', 4210))

            # TODO: remove this
            reset_buzzer()

        try:
            data, addr = udp_sync_socket.recvfrom(128)

            # FIXME
            if addr[0] != '192.168.0.247':
                print(tick, "tick from", addr, int.from_bytes(data, byteorder='little'))

                c = get_client(addr=addr[0])
                if c is None:
                    print("open TCP socket")
                    c = {'socket': socket.socket(socket.AF_INET, socket.SOCK_STREAM), 'addr': addr[0]}
                    c['socket'].settimeout(1)
                    c['socket'].connect((addr[0], 9999))
                    clients.append(c)

        except socket.error:
            pass  # no data yet

        socket_list = []
        for c in clients:
            socket_list.append(c['socket'])

        # Get the list sockets which are readable
        read_sockets, write_sockets, error_sockets = select.select(socket_list, [], socket_list, 0.1)

        for sock in read_sockets:
            data = sock.recv(128)
            if data[0] == 0x01 and len(data) == 5:
                print(tick, "BUZZERED", sock.getpeername(), int.from_bytes(data, byteorder='little'))
            elif data[0] == 0xAA:
                c = get_client(socket=sock)
                c['heartbeat'] = tick
            else:
                print(sock.getpeername(), "unknown server response")

        for sock in error_sockets:
            print("error socket", sock)
            assert (False)

        for c in clients:
            if 'heartbeat' in c and tick - c['heartbeat'] > 2000:
                print(c['socket'].getpeername(), "Heartbeat timeout")
                clients.remove(c)

        time.sleep(0.1)

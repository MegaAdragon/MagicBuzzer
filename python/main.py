import math
import socket
import time
import math
import select
import threading

from flask import Flask, render_template, request, jsonify, make_response

clients = []

app = Flask(__name__)

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
        c['buzzered'] = False
        c['buzzerTick'] = 0


@app.route('/')
def home():
    return render_template('buzzer.html', clients=clients)


@app.route('/api/v1/buzzer/all', methods=['GET'])
def api_all():
    buzzer_list = []
    for c in clients:
        buzzer = {'addr': c['addr']}

        if 'buzzered' in c:
            buzzer['buzzered'] = c['buzzered']
            buzzer['buzzerTick'] = c['buzzerTick']

        if c['addr'] == '192.168.0.70':
            buzzer['color'] = '#478eff'
        elif c['addr'] == '192.168.0.183':
            buzzer['color'] = '#fcf568'
        buzzer_list.append(buzzer)
            
    return jsonify(buzzer_list)

@app.route('/api/v1/buzzer/reset', methods=['POST'])
def api_reset():
    reset_buzzer()
    return make_response("Success", 200)


if __name__ == '__main__':
    # run flask in own thread
    threading.Thread(target=app.run, daemon=True, kwargs={'host': 'localhost', 'port': 5000, 'debug': False}).start()

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
            print("sync broadcast send", math.ceil(tick))
            udp_sync_socket.sendto(math.ceil(tick).to_bytes(4, byteorder='little'), ('<broadcast>', 4210))

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
                buzzerTick = int.from_bytes(data[1:], byteorder='little')
                print(tick, "BUZZERED", sock.getpeername(), buzzerTick)
                c = get_client(socket=sock)
                c['buzzered'] = True
                c['buzzerTick'] = buzzerTick
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

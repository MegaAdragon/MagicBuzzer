import math
import socket
import time
import math
import select
import threading
import logging

from flask import Flask, render_template, request, jsonify, make_response

clients = []

app = Flask(__name__)

log = logging.getLogger('werkzeug')
log.setLevel(logging.ERROR)


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

    # FIXME: this is not very efficient
    for c in clients:
        buzzer = {'addr': c['addr']}

        if 'buzzered' in c:
            buzzer['buzzered'] = c['buzzered']
            buzzer['buzzerTick'] = c['buzzerTick']

        if c['addr'] == '192.168.0.23':
            buzzer['color'] = '#478eff' # blue
        elif c['addr'] == '192.168.0.24':
            buzzer['color'] = '#fcf568' # yellow
        elif c['addr'] == '192.168.0.15':
            buzzer['color'] = '#ff5e5e' # red
        elif c['addr'] == '192.168.0.30':
            buzzer['color'] = '#35d45f' # green
        buzzer_list.append(buzzer)
            
    return jsonify(buzzer_list)


@app.route('/api/v1/buzzer/reset', methods=['POST'])
def api_reset():
    reset_buzzer()
    return make_response("Success", 200)


def check_buzzer_pos():
    buzzered_clients = []
    for c in clients:
        if 'buzzered' in c and c['buzzered']:
            buzzered_clients.append(c)

    buzzered_clients = sorted(buzzered_clients, key=lambda c: c['buzzerTick'])

    # for safety reasons
    if len(buzzered_clients) == 0 or tick - buzzered_clients[0]['tick'] < 300:
        return

    pos = 1
    for c in buzzered_clients:
        c['socket'].sendall(bytearray([0x20, pos, 0xFF]))
        pos = pos + 1


def handle_data(sock, cmd, data):
    if cmd == 0x01 and len(data) >= 5:
        c = get_client(socket=sock)
        c['buzzered'] = True
        c['buzzerTick'] = int.from_bytes(data[:4], byteorder='little')
        c['tick'] = tick
        c['src'] = data[4]
        print(tick, "BUZZERED", sock.getpeername(), c['src'], c['buzzerTick'])
        check_buzzer_pos()
    elif cmd == 0xAA:
        c = get_client(socket=sock)
        c['heartbeat'] = tick
        c['voltage'] = int.from_bytes(data[:2], byteorder='little', signed=False) / 1000.0
        c['rssi'] = data[2]
    else:
        print(sock.getpeername(), "unknown client response", cmd, data)
        assert False


data_buffer = bytes()


def on_data_received(sock, data):
    global data_buffer
    data_buffer += data
    header_size = 2

    while True:
        if len(data_buffer) < header_size:
            # not enough data
            break

        header = data_buffer[:header_size]
        size = header[1]

        if len(data_buffer) < header_size + size:
            # wait for complete payload
            break

        # Read the content of the message body
        body = data_buffer[header_size:header_size + size]

        # data processing
        handle_data(sock, header[0], body)

        # Get the next packet
        data_buffer = data_buffer[header_size + size:]


if __name__ == '__main__':
    # run flask in own thread
    threading.Thread(target=app.run, daemon=True, kwargs={'host': '0.0.0.0', 'port': 5001, 'debug': False}).start()

    start_time = time.time()
    udp_sync_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    udp_sync_socket.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)

    own_address = socket.gethostbyname(socket.gethostname())
    print("Host:", own_address)

    # listen to this port
    udp_sync_socket.bind((own_address, 4210))
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

            if addr[0] != own_address:
                print(tick, "tick from", addr, int.from_bytes(data, byteorder='little'))

                c = get_client(addr=addr[0])
                if c is None:
                    print("open TCP socket")
                    c = {'socket': socket.socket(socket.AF_INET, socket.SOCK_STREAM), 'addr': addr[0]}
                    c['socket'].settimeout(1)
                    c['socket'].connect((addr[0], 9999))
                    c['heartbeat'] = tick
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
            on_data_received(sock, data)

        for sock in error_sockets:
            print("error socket", sock)
            assert False

        for c in clients:
            if tick - c['heartbeat'] > 3000:
                print(c['socket'].getpeername(), "Heartbeat timeout")
                clients.remove(c)

        check_buzzer_pos()

        time.sleep(0.1)

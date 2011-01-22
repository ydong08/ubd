#! /usr/bin/python
import sys
import serialinterface
import time
import socket
import SocketServer

import thread
import threading
import Queue
import signal
import sys

running = True
def signal_handler(signal, frame):
    global running
    running = False
signal.signal(signal.SIGINT, signal_handler)

out = Queue.Queue(5)

class TCPHandler(SocketServer.BaseRequestHandler):
    def parse(self, data):
        if self.state == 0:
            if data == 'C':
                self.state = 1
                self.cmd = ''
            elif data == 'B':
                self.state = 2
                self.cmd = ''
        elif self.state == 1:
            if data == '\n' or data == '\r':
                self.state = 0
                return self.cmd
            elif ord(data) < 0x20:
                self.state = 4
            else:
                self.cmd += data
        elif self.state == 2:
            self.cmdbinlen = ord(data)
            self.state = 3
        elif self.state == 3:
            self.cmd += data
            self.cmdbinlen -= 1
            if self.cmdbinlen == 0:
                self.state = 0
                return self.cmd
        elif self.state == 4:
            if data == '\n' or data == '\r':
                self.state = 0
        return 0

    def handle(self):
        self.state = 0
        while 1:
            self.data = self.request.recv(1)
            if not self.data:
                break
            m = self.parse(self.data)
            if m:
                header = '\x01\x02\x00%c'%len(m);
                out.put(header + m)
                out.join()
                self.request.send("A")

class V6Server(SocketServer.ThreadingTCPServer):
    address_family = socket.AF_INET6
    allow_reuse_address = True

def TCPServer():
    server = V6Server(("::", 2323), TCPHandler)
    while True:
        server.handle_request()

def UDPServer():
    sock = socket.socket(socket.AF_INET6,socket.SOCK_DGRAM,socket.IPPROTO_UDP)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(('', 2323))

    while True:
        data = sock.recv(128)
        if len(data) < 30:
            header = '\x01\xff\x08%c'%len(data);
            out.put(header + data)
        

serial = serialinterface.SerialInterface('/dev/ttyUSB0', 115200, 1)

def initbridge():
    while True:
        serial.writeMessage("B")
        time.sleep(2)
        m = serial.readMessage();
        if m == 'B':
            #serial.writeMessage("\x01\x02\x10\x01O")
            break

initbridge()
thread.start_new_thread(TCPServer,())
thread.start_new_thread(UDPServer,())

while running:
    try:
        data = out.get(True, 1)
        serial.writeMessage(data)
        while True:
            m = serial.readMessage()
            if m == 'S':
                break
            elif m == 's':
                initbridge()
                break

        out.task_done()
    except:
        pass

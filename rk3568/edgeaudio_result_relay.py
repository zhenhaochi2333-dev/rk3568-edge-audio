#!/usr/bin/env python3
"""Fan out the board result stream for multiple display clients.

The formal receiver keeps one result subscriber for compatibility. This
display-side relay consumes that single upstream connection and broadcasts the
unchanged newline-delimited JSON to the PC GUI and the board GUI.
"""

from __future__ import print_function

import argparse
import socket
import threading
import time


class ResultRelay(object):
    def __init__(self, upstream_host, upstream_port, listen_host, listen_port):
        self.upstream = (upstream_host, upstream_port)
        self.listen_host = listen_host
        self.listen_port = listen_port
        self.stop_event = threading.Event()
        self.clients = set()
        self.clients_lock = threading.Lock()
        self.server = None

    def run(self):
        self.server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.server.bind((self.listen_host, self.listen_port))
        self.server.listen(8)
        threading.Thread(target=self._accept_loop, daemon=True).start()
        print("EDGEAUDIO_RELAY_READY upstream=%s:%d listen=%s:%d" % (
            self.upstream[0], self.upstream[1], self.listen_host, self.listen_port), flush=True)
        self._upstream_loop()

    def _accept_loop(self):
        while not self.stop_event.is_set():
            try:
                client, address = self.server.accept()
                client.settimeout(2.0)
                with self.clients_lock:
                    self.clients.add(client)
                print("RELAY_CLIENT_CONNECTED %s:%d" % address, flush=True)
            except socket.timeout:
                continue
            except OSError:
                if not self.stop_event.is_set():
                    time.sleep(0.2)

    def _upstream_loop(self):
        while not self.stop_event.is_set():
            try:
                upstream = socket.create_connection(self.upstream, timeout=5)
                # The result stream may remain silent for an arbitrary time.
                # Keep the connected socket blocking; a read timeout here would
                # create a fake reconnect loop during normal silence.
                upstream.settimeout(None)
                print("RELAY_UPSTREAM_CONNECTED", flush=True)
                with upstream:
                    upstream_file = upstream.makefile("rb")
                    while not self.stop_event.is_set():
                        line = upstream_file.readline()
                        if not line:
                            break
                        self._broadcast(line)
                print("RELAY_UPSTREAM_CLOSED", flush=True)
            except OSError as exc:
                print("RELAY_UPSTREAM_RETRY %s" % exc, flush=True)
            if not self.stop_event.is_set():
                self.stop_event.wait(1.0)

    def _broadcast(self, data):
        with self.clients_lock:
            dead = []
            for client in self.clients:
                try:
                    client.sendall(data)
                except OSError:
                    dead.append(client)
            for client in dead:
                self.clients.discard(client)
                try:
                    client.close()
                except OSError:
                    pass

    def stop(self):
        self.stop_event.set()
        if self.server:
            try:
                self.server.close()
            except OSError:
                pass
        with self.clients_lock:
            for client in self.clients:
                try:
                    client.close()
                except OSError:
                    pass
            self.clients.clear()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--upstream-host", default="127.0.0.1")
    parser.add_argument("--upstream-port", type=int, default=5701)
    parser.add_argument("--listen-host", default="0.0.0.0")
    parser.add_argument("--listen-port", type=int, default=5702)
    args = parser.parse_args()
    relay = ResultRelay(args.upstream_host, args.upstream_port, args.listen_host, args.listen_port)
    try:
        relay.run()
    except KeyboardInterrupt:
        pass
    finally:
        relay.stop()


if __name__ == "__main__":
    main()

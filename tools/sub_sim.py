#!/usr/bin/env python3
"""
sub_sim.py - a scriptable sub vendo that speaks the real signed protocol
(common/zx_protocol.h, tools/zx_protocol.py). Placeholder until Task 5.
"""


class SubSim:
    def __init__(self, host="localhost", port=8098, mac="AA:BB:CC:DD:EE:01"):
        self.host, self.port, self.mac = host, port, mac

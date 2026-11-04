# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 erhansavas
"""The fixed views every picture gate uses, including the frames the owner found fault with, kept as tests forever."""

DEFAULT_EYE = "0,0.24,0.26,0,0,0,34"
CLOSE_FLAT = "0.0,0.065,0.040,0.0,0.022,0.022,24"

VIEWS = [
    ("rear_owner", {"hold": "130,60,0"}),
    ("rear_110", {"hold": "110,40,0"}),
    ("rear_150", {"hold": "150,50,0"}),
    ("rear_70", {"hold": "70,50,0"}),
    ("close_owner", {"hold": "14,-10,33", "eye": "0.012,0.150,0.075,0.006,0.012,-0.018,37"}),
    ("close_trigger", {"hold": "120,30,0", "eye": "0.09,-0.02,-0.11,0.05,0.0,-0.05,24"}),
    ("close_trigger_pulled", {"hold": "120,30,0", "eye": "0.09,-0.02,-0.11,0.05,0.0,-0.05,24", "press": "trigger=1,gear=0.48"}),
    ("close_stick", {"hold": "0,0,0", "eye": "0.042,0.075,0.050,0.040,0.022,0.040,30"}),
    ("close_cross", {"hold": "0,0,0", "eye": "-0.060,0.072,-0.010,-0.060,0.012,-0.010,30"}),
    ("close_flat", {"hold": "0,0,0", "eye": CLOSE_FLAT}),
    ("pressed_all", {"hold": "20,0,0", "press": "sticks=1,keys=1,cross=1,trigger=1,shoulders=1,gear=0.48"}),
]
for yaw in range(0, 360, 15):
    pitch = {0: 20, 1: -35, 2: 35, 3: 130}[(yaw // 15) % 4]
    VIEWS.append((f"orbit_{yaw:03d}", {"hold": f"{pitch},{yaw},0"}))

# Baseline frames measured again with the raw error (the first measurement read the colour map by mistake), and each threshold
# keeps the same ratio to its baseline: flat .005/.032, stick .0072/.043, cross .0158/.158, trigger .0279/.173, edges from .144.
CLOSE = {"close_flat": (0.0045, 0.027), "close_stick": (0.006, 0.034), "close_cross": (0.012, 0.087), "close_trigger": (0.015, 0.090),
         "close_trigger_pulled": (0.015, 0.090), "close_owner": (0.015, 0.090)}
CLOSE_EDGE = 0.128
# The same for the object in orbit: baseline mean .036, p99 .270, p99.9 .411, edges .460.
ORBIT = (0.027, 0.165, 0.32, 0.317)


def arguments(view):
    args = ["--hold", view.get("hold", "0,0,0"), "--eye", view.get("eye", DEFAULT_EYE)]
    if "press" in view:
        args += ["--press", view["press"]]
    return args

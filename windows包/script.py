# -*- coding: utf-8 -*-
"""Recvt Listener v24. Copy script.py, recvt_bridge.py, recvt_model.py to the button."""
import os
import sys
from System import AppDomain
__persistentengine__ = True

# Must match the Qt script directory. Exports are stored in its output subdirectory.
SCRIPTS_DIR = r"D:\revit_ai_script"
button_dir = os.path.dirname(os.path.abspath(__file__))
if button_dir not in sys.path:
    sys.path.insert(0, button_dir)
import recvt_bridge
import recvt_model
try:
    reload(recvt_bridge)
    reload(recvt_model)
except NameError:
    import importlib
    importlib.reload(recvt_bridge)
    importlib.reload(recvt_model)

if not os.path.isdir(SCRIPTS_DIR):
    os.makedirs(SCRIPTS_DIR)
for key in ("RecvtAI_IdlingHandler_v16", "RecvtAI_IdlingHandler_v18", "RecvtAI_IdlingHandler_v19", "RecvtAI_IdlingHandler_v20", "RecvtAI_IdlingHandler_v24"):
    old = AppDomain.CurrentDomain.GetData(key)
    if old is not None:
        try:
            __revit__.Idling -= old
        except Exception:
            pass
        AppDomain.CurrentDomain.SetData(key, None)

handler = recvt_bridge.make_handler(__revit__, SCRIPTS_DIR)
__revit__.Idling += handler
AppDomain.CurrentDomain.SetData("RecvtAI_IdlingHandler_v24", handler)
recvt_bridge.atomic_json(os.path.join(SCRIPTS_DIR, 'listener_status.json'),
                         {'state': 'started', 'directory': SCRIPTS_DIR, 'version': 24})
print("Recvt Listener v24 / Qt 6 started: " + SCRIPTS_DIR)
print("Warnings are dismissed during AI tasks; errors roll back. Other confirmations use dialog_rules.json.")

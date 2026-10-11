# -*- coding: utf-8 -*-
# recvt_request_id: sample-two-storey-v18-001
"""Optional offline smoke task: 10 x 8 m, two 3 m storeys, 8 walls, 3 slabs.
Copy to the communication directory as ai_gen.py. Run once in an empty project.
"""
from pyrevit import revit, DB
import recvt_model as building

doc = revit.doc
transaction = DB.Transaction(doc, 'Recvt two storey smoke test')
transaction.Start()
try:
    outline = [(0, 0), (10, 0), (10, 8), (0, 8)]
    levels = [building.level(doc, 'Recvt L{}'.format(i), i * 3.0) for i in range(3)]
    for base in levels[:2]:
        for i in range(4):
            building.wall(doc, base, outline[i], outline[(i + 1) % 4], 3.0)
    for base in levels:
        building.floor(doc, base, outline)
    if transaction.Commit() != DB.TransactionStatus.Committed:
        raise Exception('Sample transaction did not commit')
except Exception:
    if transaction.GetStatus() == DB.TransactionStatus.Started:
        transaction.RollBack()
    raise
finally:
    transaction.Dispose()

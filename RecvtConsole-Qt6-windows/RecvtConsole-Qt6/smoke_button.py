# -*- coding: utf-8 -*-
"""Queue the deterministic model through the same listener as the Qt app."""
import codecs
import os
import uuid
import sys
from System import AppDomain
from System.IO import File

directory = r'D:\revit_ai_script'
__persistentengine__ = True
listener_directory = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                                 'AI_Listener.pushbutton')
if AppDomain.CurrentDomain.GetData('RecvtAI_IdlingHandler_v20') is None:
    listener_path = os.path.join(listener_directory, 'script.py')
    namespace = {'__file__': listener_path, '__name__': '__main__', '__revit__': __revit__}
    with codecs.open(listener_path, 'r', 'utf-8-sig') as stream:
        exec(compile(stream.read(), listener_path, 'exec'), namespace)
source = os.path.join(listener_directory, 'sample_two_storey.py')
with codecs.open(source, 'r', 'utf-8-sig') as stream:
    lines = stream.read().splitlines()
request_id = uuid.uuid4().hex
lines = [line for line in lines if not line.startswith('# recvt_request_id:')]
lines.insert(1, '# recvt_request_id: ' + request_id)
if not os.path.isdir(directory):
    os.makedirs(directory)
target = os.path.join(directory, 'ai_gen.py')
temporary = target + '.' + request_id + '.tmp'
with codecs.open(temporary, 'w', 'utf-8') as stream:
    stream.write('\n'.join(lines) + '\n')
if File.Exists(target):
    File.Replace(temporary, target, None)
else:
    File.Move(temporary, target)
print('Queued two-storey test: ' + request_id)
print('8 walls and 3 slabs, 10 x 8 metres. Check result.json for the result.')

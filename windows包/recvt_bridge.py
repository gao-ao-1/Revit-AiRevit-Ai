# -*- coding: utf-8 -*-
"""Revit 2025 / IronPython 2.7 bridge. Called only from Revit Idling."""
import os
import json
import codecs
import time
import traceback
import ast

try:
    text_type = unicode
except NameError:
    text_type = str


def check_model_code(code, filepath):
    """Reject wall LocationCurve.Point access inferred from wall creation/collectors."""
    tree = ast.parse(code, filepath)
    for node in ast.walk(tree):
        if isinstance(node, ast.Attribute) and node.attr == 'StartNewStairs':
            raise ValueError('Revit 2025 has no StartNewStairs. Use building.stairs_u AFTER committing the building transaction; StairsEditScope.Start(baseLevel.Id, topLevel.Id) is the real API.')
    def dotted(node):
        if isinstance(node, ast.Name):
            return node.id
        if isinstance(node, ast.Attribute):
            return dotted(node.value) + '.' + node.attr
        return ''
    containers = {}
    for node in ast.walk(tree):
        if isinstance(node, ast.Assign) and isinstance(node.value, ast.Call):
            kind = dotted(node.value.func).split('.')[-1]
            if kind in ('CurveArray', 'CurveLoop'):
                for target in node.targets:
                    if isinstance(target, ast.Name):
                        containers[target.id] = kind
    for node in ast.walk(tree):
        if not isinstance(node, ast.Call):
            continue
        if dotted(node.func).endswith('.Create.NewFloor'):
            raise ValueError('Revit 2025: use DB.Floor.Create(doc, List[DB.CurveLoop], floorType.Id, level.Id); NewFloor is obsolete.')
        if isinstance(node.func, ast.Attribute) and node.func.attr == 'Append' and node.args:
            if containers.get(dotted(node.func.value)) == 'CurveArray' and containers.get(dotted(node.args[0])) == 'CurveLoop':
                raise ValueError('CurveArray.Append expects ONE Curve, not CurveLoop. Append each curve separately; floor contours use List[DB.CurveLoop].')
    scopes = [tree] + [n for n in ast.walk(tree) if isinstance(n, ast.FunctionDef)]
    records = []
    functions = {}
    for scope in scopes:
        nodes = []
        def visit(node):
            nodes.append(node)
            for child in ast.iter_child_nodes(node):
                if not isinstance(child, (ast.FunctionDef, ast.ClassDef)):
                    visit(child)
        visit(scope)
        record = {'scope': scope, 'nodes': nodes, 'walls': set(), 'lists': set(), 'returns': False}
        records.append(record)
        if isinstance(scope, ast.FunctionDef):
            functions[scope.name] = record
    changed = True
    while changed:
        before = repr([(sorted(r['walls']), sorted(r['lists']), r['returns']) for r in records])
        for r in records:
            for n in r['nodes']:
                if isinstance(n, ast.Assign):
                    names = [t.id for t in n.targets if isinstance(t, ast.Name)]
                    v = n.value
                    if isinstance(v, ast.Call):
                        name = dotted(v.func)
                        if name in ('building.wall', 'DB.Wall.Create'):
                            r['walls'].update(names)
                        if (name.endswith('.OfClass') and v.args and dotted(v.args[0]) == 'DB.Wall') or (name in functions and functions[name]['returns']):
                            r['lists'].update(names)
                    elif isinstance(v, ast.Name):
                        if v.id in r['lists']: r['lists'].update(names)
                        if v.id in r['walls']: r['walls'].update(names)
                if isinstance(n, ast.For) and isinstance(n.iter, ast.Name) and n.iter.id in r['lists'] and isinstance(n.target, ast.Name):
                    r['walls'].add(n.target.id)
                if isinstance(n, ast.Call):
                    if isinstance(n.func, ast.Attribute) and n.func.attr == 'append' and n.args and isinstance(n.args[0], ast.Name) and n.args[0].id in r['walls'] and isinstance(n.func.value, ast.Name):
                        r['lists'].add(n.func.value.id)
                    name = dotted(n.func)
                    if name in functions:
                        dest = functions[name]
                        for parameter, argument in zip(dest['scope'].args.args, n.args):
                            argname = getattr(parameter, 'arg', getattr(parameter, 'id', ''))
                            if isinstance(argument, ast.Name):
                                if argument.id in r['lists']: dest['lists'].add(argname)
                                if argument.id in r['walls']: dest['walls'].add(argname)
                if isinstance(n, ast.Return) and isinstance(n.value, ast.Name) and n.value.id in r['lists']:
                    r['returns'] = True
        changed = before != repr([(sorted(r['walls']), sorted(r['lists']), r['returns']) for r in records])
    for r in records:
        aliases = set()
        for n in r['nodes']:
            if isinstance(n, ast.Assign) and isinstance(n.value, ast.Attribute):
                v = n.value
                if v.attr == 'Location' and isinstance(v.value, ast.Name) and v.value.id in r['walls']:
                    aliases.update(t.id for t in n.targets if isinstance(t, ast.Name))
        for n in r['nodes']:
            if isinstance(n, ast.Attribute) and n.attr == 'Point':
                alias = isinstance(n.value, ast.Name) and n.value.id in aliases
                direct = dotted(n.value).endswith('.Location') and dotted(n.value).split('.')[0] in r['walls']
                if alias or direct:
                    raise ValueError('Line {}: wall Location is LocationCurve, not LocationPoint. '
                                     'Keep building.wall returned hosts; use building.opening_at or '
                                     'Location.Curve.GetEndPoint with correct feet/metres conversion.'.format(n.lineno))


def atomic_json(path, value):
    from System.IO import File
    temporary = path + '.tmp'
    with codecs.open(temporary, 'w', 'utf-8') as stream:
        stream.write(json.dumps(value, ensure_ascii=False))
    if os.path.exists(path):
        File.Replace(temporary, path, None)
    else:
        File.Move(temporary, path)


def read_json(path):
    try:
        with codecs.open(path, 'r', 'utf-8-sig') as stream:
            return json.load(stream)
    except Exception:
        return {}


def element_name(element):
    from Autodesk.Revit import DB
    try:
        return text_type(DB.Element.Name.GetValue(element))
    except Exception:
        return str(element.Id.Value)


def write_context(doc, directory):
    from Autodesk.Revit import DB
    data = {'document': text_type(doc.Title), 'revit_version': text_type(doc.Application.VersionNumber),
            'length_unit': 'feet', 'updated_at': time.time(), 'levels': [], 'types': []}
    for level in DB.FilteredElementCollector(doc).OfClass(DB.Level):
        # CLR Int64 is not always accepted by IronPython's JSON encoder.
        # IDs are identifiers, not quantities; decimal strings also avoid JSON
        # double precision loss when the Qt client reads large 64-bit values.
        data['levels'].append({'id': text_type(level.Id.Value), 'name': element_name(level),
                               'elevation': float(level.Elevation)})
    for cls in (DB.WallType, DB.FloorType, DB.RoofType, DB.FamilySymbol):
        count = 0
        for item in DB.FilteredElementCollector(doc).OfClass(cls):
            if count >= 150:
                break
            data['types'].append({'id': text_type(item.Id.Value), 'name': element_name(item),
                                  'family': text_type(getattr(item, 'FamilyName', '')),
                                  'category': text_type(item.Category.Name) if item.Category else ''})
            count += 1
    atomic_json(os.path.join(directory, 'revit_context.json'), data)


def model_element_ids(doc):
    from Autodesk.Revit import DB
    return set(text_type(item.Id.Value) for item in
               DB.FilteredElementCollector(doc).WhereElementIsNotElementType())


def parameter_values(element):
    """Read-only display values, formatted using the Revit project's units."""
    from Autodesk.Revit import DB
    values = []
    for parameter in element.Parameters:
        try:
            if not parameter.HasValue:
                continue
            storage = parameter.StorageType
            if storage == DB.StorageType.String:
                value = parameter.AsString()
            elif storage == DB.StorageType.ElementId:
                ref_id = parameter.AsElementId()
                target = element.Document.GetElement(ref_id)
                value = (element_name(target) + ' [' + text_type(ref_id.Value) + ']'
                         if target else text_type(ref_id.Value))
            else:
                value = parameter.AsValueString()
                if value is None and storage == DB.StorageType.Integer:
                    value = text_type(parameter.AsInteger())
                # Do not label an unformatted internal-unit double as a display value.
            if value is not None:
                values.append({'id': text_type(parameter.Id.Value),
                               'name': text_type(parameter.Definition.Name),
                               'value': text_type(value)})
        except Exception:
            continue
    return sorted(values, key=lambda item: (item['name'], item['id']))


def element_metadata(doc, element, types):
    from Autodesk.Revit import DB
    type_id = element.GetTypeId()
    key = text_type(type_id.Value)
    element_type = doc.GetElement(type_id)
    if element_type is not None and key not in types:
        types[key] = {'name': element_name(element_type),
                      'family': text_type(getattr(element_type, 'FamilyName', '')),
                      'parameters': parameter_values(element_type)}
    level = doc.GetElement(element.LevelId)
    if level is None:
        for name in ('FAMILY_LEVEL_PARAM', 'WALL_BASE_CONSTRAINT', 'ROOF_BASE_LEVEL_PARAM',
                     'STAIRS_BASE_LEVEL_PARAM', 'INSTANCE_REFERENCE_LEVEL_PARAM'):
            try:
                parameter = element.get_Parameter(getattr(DB.BuiltInParameter, name))
                if parameter and parameter.StorageType == DB.StorageType.ElementId:
                    candidate = doc.GetElement(parameter.AsElementId())
                    if isinstance(candidate, DB.Level):
                        level = candidate
                        break
            except Exception:
                pass
    category = element.Category
    return {'id': text_type(element.Id.Value), 'unique_id': text_type(element.UniqueId),
            'name': element_name(element), 'type_id': key,
            'category': text_type(category.Name) if category else '',
            'category_id': text_type(category.Id.Value) if category else '',
            'level': element_name(level) if level else '',
            'level_id': text_type(level.Id.Value) if level else '',
            'parameters': parameter_values(element)}


def export_mesh(doc, path, element_ids=None):
    from Autodesk.Revit import DB
    options = DB.Options()
    options.DetailLevel = DB.ViewDetailLevel.Fine
    options.IncludeNonVisibleObjects = False
    counts = [0, 0]
    skipped = [0]
    metadata = {'schema_version': 1, 'document': text_type(doc.Title),
                'length_unit': 'feet', 'up_axis': 'Z', 'scope': 'project' if element_ids is None else 'task',
                'parameter_units': 'Revit project display units',
                'elements': [], 'types': {}, 'materials': {}}
    current_materials = set()
    fallback_material = [None]

    def material_key(material_id):
        try:
            material = doc.GetElement(material_id) if material_id is not None else None
            if not isinstance(material, DB.Material):
                material = fallback_material[0]
            if material is None:
                return 'default'
            key = 'material_' + text_type(material.Id.Value)
            if key not in metadata['materials']:
                color = material.Color
                metadata['materials'][key] = {
                    'name': element_name(material),
                    'color': [int(color.Red), int(color.Green), int(color.Blue)],
                    'opacity': 1.0 - float(material.Transparency) / 100.0}
            return key
        except Exception:
            return 'default'
    # Stream triangles rather than retaining the entire model in Python memory.
    try:
        with codecs.open(path, 'w', 'utf-8') as stream:
            stream.write('# Revit coordinates: feet, Z up\n')

            def mesh_triangles(mesh, transform, material_id=None):
                key = material_key(material_id)
                current_materials.add(key)
                stream.write('usemtl {}\n'.format(key))
                for i in range(mesh.NumTriangles):
                    tri = mesh.get_Triangle(i)
                    points = [transform.OfPoint(tri.get_Vertex(j)) for j in range(3)]
                    for p in points:
                        stream.write('v {} {} {}\n'.format(p.X, p.Y, p.Z))
                    start = counts[0] + 1
                    # Mirror transforms need reversed winding.
                    order = (start, start + 2, start + 1) if transform.HasReflection else (start, start + 1, start + 2)
                    stream.write('f {} {} {}\n'.format(*order))
                    counts[0] += 3
                    counts[1] += 1

            def collect(geometry, transform):
                for obj in geometry:
                    if isinstance(obj, DB.Solid):
                        for face in obj.Faces:
                            mesh_triangles(face.Triangulate(), transform, face.MaterialElementId)
                    elif isinstance(obj, DB.Mesh):
                        mesh_triangles(obj, transform, getattr(obj, 'MaterialElementId', None))
                    elif isinstance(obj, DB.GeometryInstance):
                        child = obj.GetSymbolGeometry()
                        if child is not None:
                            collect(child, transform.Multiply(obj.Transform))

            for element in DB.FilteredElementCollector(doc).WhereElementIsNotElementType():
                if element_ids is not None and text_type(element.Id.Value) not in element_ids:
                    continue
                start_triangles = counts[1]
                current_materials.clear()
                fallback_material[0] = None
                # Object names are stable IDs, never user-controlled names with whitespace.
                stream.write('o element_{}\n'.format(text_type(element.Id.Value)))
                geometry_error = False
                try:
                    if element.Category:
                        fallback_material[0] = element.Category.Material
                except Exception:
                    pass
                try:
                    geometry = element.get_Geometry(options)
                    if geometry is not None:
                        collect(geometry, DB.Transform.Identity)
                except Exception:
                    skipped[0] += 1
                    geometry_error = True
                if counts[1] > start_triangles:
                    try:
                        info = element_metadata(doc, element, metadata['types'])
                    except Exception:
                        info = {'id': text_type(element.Id.Value), 'name': element_name(element),
                                'metadata_warning': 'Some element properties could not be read.'}
                    info['triangles'] = counts[1] - start_triangles
                    info['materials'] = sorted(current_materials)
                    if geometry_error:
                        info['geometry_warning'] = 'Some geometry could not be exported.'
                    metadata['elements'].append(info)
    finally:
        options.Dispose()
    # A valid empty project must clear the previous preview after deleting its last element.
    # A wholly failed export must not masquerade as an empty project.
    if counts[1] == 0 and skipped[0]:
        raise Exception('No triangles exported; {} elements failed.'.format(skipped[0]))
    atomic_json(os.path.join(os.path.dirname(path), 'elements.json'), metadata)
    return counts[1], skipped[0]


def export_ifc(doc, directory, element_ids=None):
    from Autodesk.Revit import DB
    options = DB.IFCExportOptions()
    options.FileVersion = DB.IFCVersion.IFC4
    options.ExportBaseQuantities = True
    options.AddOption('ExportPartsAsBuildingElements', 'true')
    options.AddOption('ExportInternalRevitPropertySets', 'true')
    options.AddOption('ExportIFCCommonPropertySets', 'true')
    transaction = DB.Transaction(doc, 'Recvt IFC export')
    try:
        if transaction.Start() != DB.TransactionStatus.Started:
            raise Exception('Cannot start IFC export transaction')
        if element_ids is not None:
            if not element_ids:
                raise ValueError('This task created no elements to export')
            from System.Collections.Generic import List
            view_type = next((item for item in DB.FilteredElementCollector(doc).OfClass(DB.ViewFamilyType)
                              if item.ViewFamily == DB.ViewFamily.ThreeDimensional), None)
            if view_type is None:
                raise ValueError('No 3D view type available for task IFC export')
            view = DB.View3D.CreateIsometric(doc, view_type.Id)
            hidden = List[DB.ElementId]()
            for item in DB.FilteredElementCollector(doc).WhereElementIsNotElementType():
                if text_type(item.Id.Value) not in element_ids and item.CanBeHidden(view):
                    hidden.Add(item.Id)
            if hidden.Count:
                view.HideElements(hidden)
            doc.Regenerate()
            options.FilterViewId = view.Id
            options.AddOption('VisibleElementsOfCurrentView', 'true')
        ok = doc.Export(directory, 'model', options)
        # IFC export requires a transaction, but its temporary document edits are discarded.
        transaction.RollBack()
        if not ok or not os.path.isfile(os.path.join(directory, 'model.ifc')):
            raise Exception('Revit IFC exporter returned no output file')
    finally:
        if transaction.GetStatus() == DB.TransactionStatus.Started:
            transaction.RollBack()
        transaction.Dispose()
        options.Dispose()


def make_failure_handler(operation):
    def on_failures(sender, args):
        from Autodesk.Revit import DB
        accessor = args.GetFailuresAccessor()
        if accessor.GetDocument() != operation['document']:
            return
        has_error = False
        for failure in accessor.GetFailureMessages():
            message = text_type(failure.GetDescriptionText())
            if failure.GetSeverity() == DB.FailureSeverity.Warning:
                operation['warnings'].append('Revit warning automatically dismissed: ' + message)
                accessor.DeleteWarning(failure)
            else:
                has_error = True
                operation['errors'].append(message)
        if has_error:
            options = accessor.GetFailureHandlingOptions()
            options.SetClearAfterRollback(True)
            accessor.SetFailureHandlingOptions(options)
            args.SetProcessingResult(DB.FailureProcessingResult.ProceedWithRollBack)
        else:
            args.SetProcessingResult(DB.FailureProcessingResult.Continue)
    return on_failures


def make_dialog_handler(directory, operation):
    # Autodesk's documented export dialog: leave temporary display mode on.
    rules = {'TaskDialog_Really_Print_Or_Export_Temp_View_Modes': 1002}
    configured = read_json(os.path.join(directory, 'dialog_rules.json'))
    if isinstance(configured, dict):
        rules.update(configured)
    def on_dialog(sender, args):
        dialog_id = text_type(args.DialogId)
        message = text_type(getattr(args, 'Message', ''))
        requested = rules.get(dialog_id)
        accepted = False
        # Result values differ across dialog types, so only explicit ID rules
        # select a button. Never apply a generic OK to unrelated prompts.
        if requested is not None:
            try:
                result_code = int(requested)
                if result_code in (1, 2, 3, 4, 5, 6, 7, 8, 1001, 1002, 1003, 1004):
                    accepted = bool(args.OverrideResult(result_code))
            except Exception:
                operation['warnings'].append('Could not apply dialog rule: ' + dialog_id)
        record = {'time': time.time(), 'dialog_id': dialog_id, 'message': message,
                  'automatically_confirmed': accepted}
        try:
            with codecs.open(os.path.join(directory, 'dialogs.log'), 'a', 'utf-8') as stream:
                stream.write(json.dumps(record, ensure_ascii=False) + '\n')
        except Exception:
            pass
        if not accepted:
            print('Recvt confirmation requires a rule: {} {}'.format(dialog_id, message))
            operation['warnings'].append('Confirmation not configured: ' + dialog_id)
    return on_dialog


def execute(code, filepath, request_id, revit, directory):
    operation = {'warnings': [], 'errors': [], 'document':
                 revit.ActiveUIDocument.Document if revit.ActiveUIDocument else None}
    failure_handler = make_failure_handler(operation)
    dialog_handler = make_dialog_handler(directory, operation)
    failure_attached = dialog_attached = False
    try:
        try:
            revit.Application.FailuresProcessing += failure_handler
            failure_attached = True
            revit.DialogBoxShowing += dialog_handler
            dialog_attached = True
        except Exception:
            operation['warnings'].append('Automatic warning/dialog handling unavailable: ' + traceback.format_exc())
        result = _execute(code, filepath, request_id, revit, directory, operation)
        result.setdefault('warnings', []).extend(operation['warnings'])
        return result
    finally:
        # Hooks apply only during this task, never to subsequent manual edits.
        if dialog_attached:
            revit.DialogBoxShowing -= dialog_handler
        if failure_attached:
            revit.Application.FailuresProcessing -= failure_handler


def _execute(code, filepath, request_id, revit, directory, operation):
    from Autodesk.Revit import DB
    result = {'request_id': request_id, 'success': False, 'stage': 'environment',
              'rolled_back': False, 'warnings': []}
    if revit.ActiveUIDocument is None:
        result['error'] = 'Open a Revit project before sending a task.'
        return result
    doc = revit.ActiveUIDocument.Document
    if doc.IsFamilyDocument or doc.IsReadOnly or doc.IsModifiable:
        result['error'] = 'Use an editable project with no other transaction active.'
        return result
    export_only = '# recvt_export_only' in code.splitlines()[:4]
    before_ids = None
    if not export_only:
        group = DB.TransactionGroup(doc, 'Recvt AI building')
        try:
            if group.Start() != DB.TransactionStatus.Started:
                raise Exception('Cannot start model transaction group')
            before_ids = model_element_ids(doc)
            namespace = {'__revit__': revit, 'doc': doc, 'uidoc': revit.ActiveUIDocument,
                         '__name__': '__main__', '__file__': filepath}
            check_model_code(code, filepath)
            exec(compile(code, filepath, 'exec'), namespace)
            if operation['errors']:
                raise Exception('Revit transaction rejected: ' + '\n'.join(operation['errors']))
            if group.Assimilate() != DB.TransactionStatus.Committed:
                raise Exception('Model transaction group did not commit')
        except Exception:
            result['stage'] = 'model'
            result['error'] = traceback.format_exc()
            try:
                if group.GetStatus() == DB.TransactionStatus.Started:
                    result['rolled_back'] = group.RollBack() == DB.TransactionStatus.RolledBack
            except Exception:
                result['error'] += '\nRollback failed; inspect Revit before retrying.'
            return result
        finally:
            group.Dispose()
    # Once committed, export errors must NEVER be reported as model failures.
    result.update(success=True, stage='complete', model_success=True,
                  ifc_success=False, mesh_success=False)
    element_ids = None
    result['export_scope'] = 'project'
    if not export_only:
        try:
            result['created_element_count'] = len(model_element_ids(doc) - before_ids)
        except Exception:
            result['warnings'].append('Cannot count created elements: ' + traceback.format_exc())
    try:
        output = os.path.join(directory, 'output', request_id)
        if not os.path.isdir(output):
            os.makedirs(output)
    except Exception:
        result['warnings'].append('Cannot create export directory: ' + traceback.format_exc())
        return result
    try:
        export_ifc(doc, output, element_ids)
        result['ifc_success'] = True
        result['ifc_path'] = 'output/{}/model.ifc'.format(request_id)
    except Exception:
        result['warnings'].append('IFC export failed (model kept): ' + traceback.format_exc())
    try:
        triangles, skipped = export_mesh(doc, os.path.join(output, 'model.obj'), element_ids)
        result['mesh_success'] = True
        result['mesh_path'] = 'output/{}/model.obj'.format(request_id)
        result['elements_path'] = 'output/{}/elements.json'.format(request_id)
        result['triangles'] = triangles
        if skipped:
            result['warnings'].append('{} elements could not be tessellated; preview may be incomplete.'.format(skipped))
    except Exception:
        result['warnings'].append('Mesh export failed (model kept): ' + traceback.format_exc())
    try:
        write_context(doc, directory)
    except Exception:
        result['warnings'].append('Could not refresh Revit context: ' + traceback.format_exc())
    return result


def make_handler(revit, directory):
    """Task journals prevent replay after a button reload or Revit restart."""
    state = {'last': 0, 'busy': False, 'context_at': 0, 'document': None}
    def on_idling(sender, args):
        if state['busy'] or time.time() - state['last'] < 1.0:
            return
        state['last'] = time.time()
        state['busy'] = True
        try:
            doc = revit.ActiveUIDocument.Document if revit.ActiveUIDocument else None
            if doc is not None and (doc != state['document'] or time.time() - state['context_at'] > 30):
                state['document'] = doc
                state['context_at'] = time.time()
                try:
                    write_context(doc, directory)
                except Exception:
                    print('Context refresh failed: ' + traceback.format_exc())
            filepath = os.path.join(directory, 'ai_gen.py')
            if not os.path.isfile(filepath):
                return
            with codecs.open(filepath, 'r', 'utf-8-sig') as stream:
                code = stream.read()
            marker = '# recvt_request_id: '
            ids = [line[len(marker):].strip() for line in code.splitlines()[:3] if line.startswith(marker)]
            # Only new Qt tasks are executed; legacy files cannot accidentally replay.
            if not ids:
                return
            request_id = ids[0]
            import re
            if not re.match(r'^[a-zA-Z0-9-]{1,80}$', request_id):
                return
            journal_dir = os.path.join(directory, 'tasks')
            if not os.path.isdir(journal_dir):
                os.makedirs(journal_dir)
            journal_path = os.path.join(journal_dir, request_id + '.json')
            if os.path.exists(journal_path):
                previous = read_json(journal_path)
                if previous.get('state') == 'done':
                    # Recover a completed result if publishing was interrupted.
                    if read_json(os.path.join(directory, 'result.json')).get('request_id') != request_id:
                        atomic_json(os.path.join(directory, 'result.json'), previous['result'])
                elif read_json(os.path.join(directory, 'result.json')).get('request_id') != request_id:
                    atomic_json(os.path.join(directory, 'result.json'), {
                        'request_id': request_id, 'success': False, 'stage': 'environment',
                        'error': 'Task was interrupted or its outcome is unknown. Inspect Revit; it will not be replayed.'})
                return
            atomic_json(journal_path, {'state': 'running', 'request_id': request_id})
            print('Recvt: executing {}'.format(request_id))
            try:
                result = execute(code, filepath, request_id, revit, directory)
            except Exception:
                result = {'request_id': request_id, 'success': False, 'stage': 'environment',
                          'error': traceback.format_exc(), 'rolled_back': False}
            atomic_json(journal_path, {'state': 'done', 'result': result})
            atomic_json(os.path.join(directory, 'result.json'), result)
            print('Recvt: finished {}, model success={}'.format(request_id, result['success']))
        except Exception:
            print(traceback.format_exc())
        finally:
            state['busy'] = False
    return on_idling

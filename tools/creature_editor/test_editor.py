#!/usr/bin/env python3
"""Pruebas reales de archivos, HTTP y mallas; no requieren navegador ni GL."""
import copy
import json
import math
from pathlib import Path
import struct
import tempfile
import threading
import unittest
from urllib.error import HTTPError
from urllib.request import Request, urlopen

import server


class EditorTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder = tempfile.TemporaryDirectory(prefix='monster-editor-test-')
        server.RECIPES = Path(cls.folder.name)
        cls.http = server.ThreadingHTTPServer(('127.0.0.1', 0), server.Handler)
        cls.http.save_lock = threading.Lock()
        cls.thread = threading.Thread(target=cls.http.serve_forever, daemon=True)
        cls.thread.start()
        cls.base = f'http://127.0.0.1:{cls.http.server_port}'

    @classmethod
    def tearDownClass(cls):
        cls.http.shutdown()
        cls.http.server_close()
        cls.thread.join()
        cls.folder.cleanup()

    def request(self, path, body=None, headers=None):
        request = Request(self.base + path,
                          data=None if body is None else json.dumps(body).encode(),
                          headers=headers or {'Content-Type': 'application/json'})
        with urlopen(request, timeout=65) as response:
            data = response.read()
            return json.loads(data) if response.headers['Content-Type'].startswith('application/json') else data

    def test_catalog_roundtrip_and_static_assets(self):
        self.assertEqual(len(self.request('/api/library')), 4)
        for i in range(1, 5):
            doc = self.request(f'/api/recipe/builtin:{i}')
            self.assertEqual(self.request('/api/validate', {'document': doc}), doc)
        for path in ('/', '/app.js', '/agent-sync.js', '/viewport.js', '/style.css'):
            self.assertGreater(len(self.request(path)), 100)

    def test_agent_transactions_schema_and_concurrency(self):
        state=self.request('/api/agent/state')
        state=self.request('/api/agent/action',{'action':'open','key':'builtin:3','expected_revision':state['revision']})
        original=copy.deepcopy(state)
        edited=self.request('/api/agent/action',{'action':'patch','expected_revision':state['revision'],
            'edits':[{'path':'/recipe/adult/head/muzzleLength','value':.57}]})
        self.assertAlmostEqual(edited['document']['recipe']['adult']['head']['muzzleLength'],.57,places=6)
        with self.assertRaises(HTTPError) as error:
            self.request('/api/agent/action',{'action':'patch','expected_revision':original['revision'],
                'edits':[{'path':'/recipe/adult/head/muzzleLength','value':.1}]})
        self.assertEqual(error.exception.code,409)
        for path,value in (('/recipe/adult/limbs/-1/length',1),('/recipe/adult/limbCount',100),('/recipe/missing',0)):
            with self.assertRaises(HTTPError):
                self.request('/api/agent/action',{'action':'patch','expected_revision':edited['revision'],'edits':[{'path':path,'value':value}]})
            self.assertEqual(self.request('/api/agent/state'),edited)
        state=self.request('/api/agent/action',{'action':'undo','expected_revision':edited['revision']})
        self.assertEqual(state['document'],original['document'])
        state=self.request('/api/agent/action',{'action':'redo','expected_revision':state['revision']})
        self.assertEqual(state['document'],edited['document'])
        state=self.request('/api/agent/action',{'action':'view','expected_revision':state['revision'],
            'view':{'stage':'juvenile','part':'head','camera':'front'}})
        self.assertEqual(state['view']['camera'],'front')
        self.assertEqual(self.request('/api/agent/state?since='+str(state['revision'])),{'revision':state['revision']})
        schema=self.request('/api/agent/schema')
        limb=schema['properties']['recipe']['properties']['adult']['properties']['limbs']['items']['properties']['archetype']
        self.assertEqual(limb['enumNames'][6],'LIMB_ARCHETYPE_UNGULATE')

    def test_agent_override_save_and_material_inspection(self):
        state=self.request('/api/agent/state')
        state=self.request('/api/agent/action',{'action':'open','key':'builtin:3','expected_revision':state['revision']})
        state=self.request('/api/agent/save',{'filename':'agent-bull.creature.json','catalog_override':True,'expected_revision':state['revision']})
        self.assertFalse(state['dirty'])
        self.assertEqual(self.request('/api/recipe/agent-bull.creature.json'),state['document'])
        self.assertIn('agent-bull.creature.json',(server.RECIPES/'catalog.txt').read_text())
        info=self.request('/api/agent/inspect',{})
        self.assertGreater(sum(m['materialCounts']['horn'] for m in info['meshes']),0)
        self.assertGreater(sum(m['materialCounts']['hoof'] for m in info['meshes']),0)
        self.assertGreater(len(info['nodes']),0)
        server.recipe_path('agent-bull.creature.json').unlink()

    def test_new_recipe_save_reload_and_overwrite(self):
        doc = self.request('/api/recipe/builtin:2')
        doc['recipe']['id'] = 1400
        doc['recipe']['name'] = 'Bosque Ámbar'
        doc['recipe']['adult']['head']['earSize'] = .81
        body = {'document': doc, 'filename': 'bosque-ambar.creature.json'}
        saved = self.request('/api/save', body)
        self.assertEqual(saved['key'], body['filename'])
        self.assertAlmostEqual(self.request('/api/recipe/' + saved['key'])['recipe']['adult']['head']['earSize'], .81, places=6)
        with self.assertRaises(HTTPError) as error:
            self.request('/api/save', body)
        self.assertEqual(error.exception.code, 409)
        body['overwrite'] = True
        body['document']['recipe']['name'] = 'Ámbar editado'
        self.request('/api/save', body)
        self.assertEqual(self.request('/api/recipe/' + saved['key'])['recipe']['name'], 'Ámbar editado')
        body['filename'] = 'otro-ambar.creature.json'
        with self.assertRaises(HTTPError) as error:
            self.request('/api/save', body)
        self.assertEqual(error.exception.code, 409)
        server.recipe_path(saved['key']).unlink()

    def test_reject_invalid_versions_counts_attachments_and_paths(self):
        base = self.request('/api/recipe/builtin:2')
        for change in ('version', 'count', 'attachment', 'root', 'nan'):
            doc = copy.deepcopy(base)
            if change == 'version': doc['version'] = 2
            elif change == 'count': doc['recipe']['bodyPlan']['moduleCount'] = 1
            elif change == 'attachment': doc['recipe']['bodyPlan']['modules'][1]['attachment']['slotId'] = 60000
            elif change == 'root': doc['recipe']['bodyPlan']['root']['localNodeId'] = 60000
            else: doc['recipe']['adult']['axial']['totalScale'] = float('nan')
            with self.assertRaises(HTTPError) as error:
                self.request('/api/validate', {'document': doc})
            self.assertEqual(error.exception.code, 422, change)
        for path in ('../escape.creature.json', '/tmp/escape.creature.json', 'evil.json'):
            with self.assertRaises(ValueError): server.recipe_path(path)
        with self.assertRaises(HTTPError) as error:
            self.request('/api/save', {'document': base, 'filename': 'builtin.creature.json'})
        self.assertEqual(error.exception.code, 422)
        with self.assertRaises(HTTPError) as error:
            self.request('/api/validate', {'document': base}, {'Content-Type': 'application/json', 'Origin': 'https://external.invalid'})
        self.assertEqual(error.exception.code, 403)

    def check_mesh(self, data):
        size, = struct.unpack_from('<I', data)
        meta = json.loads(data[4:4+size])
        self.assertGreater(len(meta['nodes']), 0)
        offset, triangles = 4 + size, 0
        for mesh in meta['meshes']:
            count = mesh['vertices']
            self.assertGreater(count, 0)
            values = list(struct.iter_unpack('<10f', data[offset:offset+count*40]))
            self.assertEqual(len(values), count)
            for vertex in values:
                self.assertTrue(all(math.isfinite(v) for v in vertex))
                self.assertTrue(all(-.001 <= v <= 1.001 for v in vertex[6:]))
            offset += count * 40
            indices = struct.unpack_from(f'<{mesh["indices"]}I', data, offset)
            self.assertLess(max(indices), count)
            self.assertEqual(len(indices) % 3, 0)
            triangles += len(indices) // 3
            offset += len(indices) * 4
        self.assertEqual(offset, len(data))
        self.assertGreater(triangles, 100)
        return triangles, len(meta['nodes'])

    def test_mesh_all_recipes_and_stages(self):
        for i in range(1, 5):
            doc = self.request(f'/api/recipe/builtin:{i}')
            for stage in range(4):
                with self.subTest(recipe=i, stage=stage):
                    mesh = self.request('/api/preview', {'document': doc, 'stage': stage})
                    print(f'  Receta {i}, etapa {stage}: {self.check_mesh(mesh)}', flush=True)

    def test_topology_edits_and_geometry_change(self):
        doc = self.request('/api/recipe/builtin:2')
        before = self.request('/api/preview', {'document': doc, 'stage': 3})
        recipe = doc['recipe']
        module = copy.deepcopy(recipe['bodyPlan']['modules'][0])
        module.update(instanceId=30, kind=4, phenotypeIndex=0,
                      attachment={'hostModuleInstanceId': 1, 'slotId': 7},
                      pathAttached=True, pathNodeA=65542, pathNodeB=65538,
                      pathU=.5, pathNormal={'x': 0, 'y': 1, 'z': 0})
        recipe['bodyPlan']['modules'].append(module)
        recipe['bodyPlan']['moduleCount'] += 1
        for stage in ('seed', 'larva', 'juvenile', 'adult'):
            recipe[stage]['ornaments'] = [{'archetype': 0, 'length': 1, 'baseRadius': .2,
                'tipRadius': .03, 'curvature': .1, 'development': 1,
                'growthDirection': {'x': 0, 'y': 1, 'z': 0}, 'lateralSweep': 0}]
            recipe[stage]['ornamentCount'] = 1
        self.request('/api/validate', {'document': doc})
        after = self.request('/api/preview', {'document': doc, 'stage': 3})
        self.assertNotEqual(before, after)
        self.assertGreater(self.check_mesh(after)[1], self.check_mesh(before)[1])


if __name__ == '__main__':
    unittest.main(verbosity=2)

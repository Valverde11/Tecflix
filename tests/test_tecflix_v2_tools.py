"""Pruebas locales y sin acceso a la red de los scripts V2.

python3 -m unittest discover -s tests -p 'test_tecflix_v2_tools.py' -v
"""
import importlib.util
import tempfile
from pathlib import Path
import unittest
from unittest.mock import patch

BASE = Path(__file__).resolve().parents[1] / 'scripts'


def module(name):
    path = BASE / (name + '.py')
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


class ToolsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.env = module('tecflix_v2_run_metadata')
        cls.lb = module('tecflix_v2_failover_check')
        cls.load = module('tecflix_v2_load_probe')

    def test_env_parser_does_not_execute_shell(self):
        with tempfile.TemporaryDirectory() as temp:
            marker = Path(temp) / 'danger'
            env = Path(temp) / '.env'
            env.write_text(f'AUTH_SECRET=$(touch {marker})\nMONGO_DB=test\n')
            with self.assertRaises(ValueError):
                self.env.parse_env(env)
            self.assertFalse(marker.exists())

    def test_env_plain_and_comments(self):
        with tempfile.TemporaryDirectory() as temp:
            env = Path(temp) / '.env'
            env.write_text('# comentario\nAUTH_SECRET="valor-prueba"\nMONGO_DB=desarrollo\n')
            self.assertEqual(self.env.parse_env(env)['AUTH_SECRET'], 'valor-prueba')
            self.assertEqual(self.env.parse_env(env)['MONGO_DB'], 'desarrollo')

    def test_tunnel_preserves_database_auth_and_query(self):
        uri = self.env.tunneled_uri('mongodb://user:pass@localhost:27017/tecflix?authSource=admin')
        self.assertIn('user:pass@127.0.0.1:27018', uri)
        self.assertIn('authSource=admin', uri)
        self.assertIn('directConnection=true', uri)

    def test_tunnel_rejects_replica_set(self):
        with self.assertRaisesRegex(ValueError, 'replicaSet'):
            self.env.tunneled_uri('mongodb://localhost:27017/?replicaSet=rs0')

    def test_failover_samples_two_backends(self):
        codes = iter([(200, 'macos'), (200, 'ubuntu'), (200, 'macos'), (200, 'ubuntu')])
        with patch.object(self.lb, 'probe', side_effect=lambda *_: next(codes)):
            with patch.object(self.lb.time, 'sleep'):
                counts, errors = self.lb.measure('http://127.0.0.1:9282', '/ready', 4, 0)
        self.assertEqual(counts, {'macos': 2, 'ubuntu': 2})
        self.assertEqual(errors, [])

    def test_failover_rejects_bad_backend_label(self):
        with patch.object(self.lb, 'probe', return_value=(200, 'maquina-A')):
            with patch.object(self.lb.time, 'sleep'):
                counts, errors = self.lb.measure('http://127.0.0.1:9282', '/ready', 4, 0)
        self.assertEqual(counts, {})
        self.assertEqual(len(errors), 4)

    def test_load_percentile(self):
        self.assertEqual(self.load.percentile([1, 2, 3, 4, 5], .95), 4)
        self.assertIsNone(self.load.percentile([], .95))


if __name__ == '__main__':
    unittest.main()

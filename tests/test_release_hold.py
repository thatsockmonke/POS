import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('release_hold',ROOT/'tools/check_release_hold.py')
guard=importlib.util.module_from_spec(spec);spec.loader.exec_module(guard)
class ReleaseHoldTests(unittest.TestCase):
 def setUp(self):
  self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
  self.gate=Path(self.temp.name)/'hold.json'
  self.gate.write_text(json.dumps({'allowed_tag':'v0.6.6-dev','reason':'Await user installation confirmation'}))
 def test_bridge_is_allowed(self):guard.check('v0.6.6-dev',self.gate)
 def test_larger_release_is_blocked_until_confirmation(self):
  with self.assertRaisesRegex(ValueError,'Await user installation'):guard.check('v0.6.7-dev',self.gate)
 def test_removing_hold_after_confirmation_allows_following_release(self):
  self.gate.unlink();guard.check('v0.6.7-dev',self.gate)
 def test_corrupt_policy_fails_closed(self):
  for body in ('broken','{}','null','{"allowed_tag":123,"reason":"hold"}'):
   self.gate.write_text(body)
   with self.assertRaises(ValueError):guard.check('v0.6.7-dev',self.gate)
 def test_unknown_policy_fields_or_invalid_allowed_tag_are_refused(self):
  for policy in ({'allowed_tag':'v0.6.6-dev','reason':'hold','release':True},{'allowed_tag':'anything','reason':'hold'}):
   self.gate.write_text(json.dumps(policy))
   with self.assertRaises(ValueError):guard.check('v0.6.6-dev',self.gate)

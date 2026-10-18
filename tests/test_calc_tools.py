from fractions import Fraction
from pathlib import Path
import random
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
class CalcToolsTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.temp=tempfile.TemporaryDirectory();cls.runner=Path(cls.temp.name)/'tools'
  subprocess.run(['gcc','-std=c11','-D_POSIX_C_SOURCE=200809L','-Wall','-Wextra','-Werror','-fsanitize=undefined','-fno-sanitize-recover=all','-I',str(ROOT/'components/pos_core/include'),str(ROOT/'tests/calc_tools_runner.c'),str(ROOT/'components/pos_core/pos_calc_tools.c'),str(ROOT/'components/pos_core/pos_calculator.c'),'-lm','-o',str(cls.runner)],check=True)
 @classmethod
 def tearDownClass(cls):cls.temp.cleanup()
 def run_tool(self,*args):
  p=subprocess.run([self.runner,*map(str,args)],capture_output=True,text=True,timeout=3);self.assertEqual(p.returncode,0,p.stderr)
  code,out=p.stdout.strip().split('\t',1) if '\t' in p.stdout.strip() else (p.stdout.strip(),'')
  return int(code),out
 def good(self,*args):
  code,out=self.run_tool(*args);self.assertEqual(code,0,out);return out
 def test_exact_fractions_against_independent_reference(self):
  rng=random.Random(108)
  for _ in range(80):
   a,b,c,d=[rng.randint(1,10000) for _ in range(4)]
   self.assertEqual(Fraction(self.good('exact',f'{a}/{b}+{c}/{d}')),Fraction(a,b)+Fraction(c,d))
  self.assertEqual(self.good('exact','0.1+0.2'),'3/10')
  self.assertEqual(self.good('exact','2^3^2'),'512')
  self.assertEqual(self.good('exact','-2^2'),'-4')
  self.assertEqual(self.good('exact','2^-3'),'1/8')
 def test_exact_range_domain_and_depth_fail_closed(self):
  for expression in ['1/0','2^64','9223372036854775807+1','9223372036854775807*2','1e3','x+1','2^'*60+'2','-'*100+'1','('*20+'1'+')'*20,'0^-1']:
   with self.subTest(expression=expression):self.assertEqual(self.run_tool('exact',expression)[0],-1)
 def test_linear_systems_and_singular(self):
  self.assertEqual(self.good('system','2,1,3;1,-1,0'),'x=1; y=1')
  self.assertEqual(self.good('system','2*x+y=3;x-y=0'),'x=1; y=1')
  self.assertEqual(self.good('system','1/2*x+y=3; x-y=0'),'x=2; y=2')
  self.assertEqual(self.run_tool('system','x*y=3;x-y=0')[0],-1)
  self.assertEqual(self.run_tool('system','x+=3;x-y=0')[0],-1)
  self.assertEqual(self.run_tool('system','2*=3;x-y=0')[0],-1)
  self.assertEqual(self.good('system','1,0,0,2;0,1,0,3;0,0,1,4'),'x=2; y=3; z=4')
  for expression in ['1,1,2;2,2,4','1,2;3,4','1,1,2;1,1,3','1,0,1e999;0,1,2']:
   self.assertEqual(self.run_tool('system',expression)[0],-1)
 def test_matrices_vectors(self):
  self.assertEqual(self.good('matrix','mul','1,2;3,4','5,6;7,8'),'19,22; 43,50')
  self.assertEqual(self.good('matrix','det','1,2;3,4',''),'-2')
  self.assertEqual(self.good('matrix','inv','1,2;3,4',''),'-2,1; 3/2,-1/2')
  self.assertEqual(self.good('matrix','dot','1,2,3','4,5,6'),'32')
  self.assertEqual(self.run_tool('matrix','inv','1,2;2,4','')[0],-1)
  self.assertEqual(self.run_tool('matrix','mul','1,2','1,2')[0],-1)
 def test_units_dimensions_temperature(self):
  self.assertAlmostEqual(float(self.good('unit',1,'mi','m')),1609.344)
  self.assertAlmostEqual(float(self.good('unit',32,'F','C')),0,delta=1e-12)
  self.assertAlmostEqual(float(self.good('unit',100,'C','F')),212)
  self.assertAlmostEqual(float(self.good('unit',36,'km/h','m/s')),10)
  self.assertEqual(self.run_tool('unit',1,'m','kg')[0],-1)
 def test_programmer_widths_shifts_and_unsigned_overflow(self):
  self.assertTrue(self.good('bits','0xff & 0x0f',8).startswith('15 / 0x0F /'))
  self.assertTrue(self.good('bits','~0',64).startswith('18446744073709551615 / 0xFFFFFFFFFFFFFFFF'))
  self.assertTrue(self.good('bits','0b1010',8).startswith('10 / 0x0A /'))
  self.assertTrue(self.good('bits','255 << 1',8).startswith('254 /'))
  for value in ['1 << 64','1 >> 8','-1','18446744073709551616','1 << -1','1 << 1x']:
   self.assertEqual(self.run_tool('bits',value,8)[0],-1)

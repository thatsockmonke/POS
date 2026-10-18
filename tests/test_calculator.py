from pathlib import Path
import math
import random
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
class CalculatorTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.temp=tempfile.TemporaryDirectory();cls.runner=Path(cls.temp.name)/'calculator'
  subprocess.run(['gcc','-std=c11','-D_POSIX_C_SOURCE=200809L','-Wall','-Wextra','-Werror','-fsanitize=undefined','-fno-sanitize-recover=all','-I',str(ROOT/'components/pos_core/include'),str(ROOT/'tests/calculator_runner.c'),str(ROOT/'components/pos_core/pos_calculator.c'),'-lm','-o',str(cls.runner)],check=True)
 @classmethod
 def tearDownClass(cls):cls.temp.cleanup()
 def commands(self,*commands):
  args=[str(self.runner)]
  for action,expression in commands:args += [action,expression]
  result=subprocess.run(args,capture_output=True,text=True,timeout=3)
  self.assertEqual(result.returncode,0,result.stderr)
  return [(int(line.split('\t',1)[0]),line.split('\t',1)[1]) for line in result.stdout.splitlines()]
 def calc(self,expression,action='eval'):
  code,out=self.commands((action,expression))[0];self.assertEqual(code,0,out);return out
 def numeric(self,expression,expected,action='eval',delta=None):
  out=self.calc(expression,action).split(' = ')[-1]
  self.assertAlmostEqual(float(out),expected,delta=delta or max(1,abs(expected))*1e-10)
 def test_precedence_fraction_roots_exponents_and_implicit_multiplication(self):
  self.assertEqual(self.calc('1/3+1/6'),'1/2 = 0.5')
  for expression,expected in [('2^3^2',512),('-2^2',-4),('2^-3',.125),('(-2)^3',-8),('2pi',2*math.pi),('2(3+4)',14),('sqrt(2)',math.sqrt(2)),('root(-27,3)',-3),('cbrt(-8)',-2),('10%',.1),('5!',120),('1e-3*1000',1),('0^0',1)]:
   with self.subTest(expression=expression):self.numeric(expression,expected)
 def test_variables_assignment_ans_and_cas_ignores_stored_x(self):
  answers=self.commands(('eval','a=12'),('eval','a/5'),('eval','ans*2'),('eval','x=99'),('solve','a*x+3=9'),('eval','x'))
  self.assertEqual([x[0] for x in answers],[0]*6)
  self.assertEqual(answers[1][1],'12/5 = 2.4')
  self.assertEqual(answers[4][1],'x = 1/2')
  self.assertEqual(answers[5][1],'99')
  answers=self.commands(('eval','a=3'),('eval','a=sqrt(-1)'),('eval','a'))
  self.assertEqual(answers[-1],(0,'3'))
 def test_scientific_functions_degrees_and_radians(self):
  for expression,expected in [('sin(pi/2)',1),('cos(pi)',-1),('tan(pi/4)',1),('asin(1)',math.pi/2),('acos(-1)',math.pi),('atan(1)',math.pi/4),('atan2(1,0)',math.pi/2),('sinh(.5)',math.sinh(.5)),('cosh(.5)',math.cosh(.5)),('tanh(-1000)',-1),('asinh(-2)',math.asinh(-2)),('acosh(2)',math.acosh(2)),('atanh(.5)',math.atanh(.5)),('ln(e)',1),('log(1000)',3),('exp(2)',math.exp(2)),('hypot(3,4)',5),('rad(180)',math.pi),('deg(pi)',180)]:
   with self.subTest(expression=expression):self.numeric(expression,expected)
  self.numeric('sin(30)',.5,'deg');self.numeric('asin(.5)',30,'deg')
 def test_rounding_statistics_combinatorics_and_integers(self):
  for expression,expected in [('abs(-7)',7),('floor(-1.2)',-2),('ceil(-1.2)',-1),('round(2.6)',3),('trunc(-1.8)',-1),('ncr(10,3)',120),('npr(10,3)',720),('gcd(48,18)',6),('lcm(6,8)',24),('mod(13,5)',3),('min(2,3)',2),('max(2,3)',3),('mean(2,4,6)',4),('std(2,4,6)',2)]:
   with self.subTest(expression=expression):self.numeric(expression,expected)
 def test_polynomial_expansion_differentiation_and_integration(self):
  self.assertEqual(self.calc('(x+1)^3','expand'),'x^3 + 3*x^2 + 3*x + 1')
  self.assertEqual(self.calc('3*x^4-2*x+7','diff'),'12*x^3 - 2')
  self.assertEqual(self.calc('3*x^2+2*x+1','integrate'),'x^3 + x^2 + x + C')
  self.assertEqual(self.calc('x/2+x/3','expand'),'5/6*x')
 def test_real_complex_repeated_and_degenerate_solutions(self):
  self.assertEqual(self.calc('2*x+3=7','solve'),'x = 2')
  self.assertEqual(self.calc('x^2-5*x+6=0','solve'),'x = 3 or 2')
  self.assertEqual(self.calc('x^2+1=0','solve'),'x = 0 +/- 1*i')
  self.assertEqual(self.calc('x^2=0','solve'),'x = 0')
  self.assertEqual(self.calc('(x-2)^2','solve'),'x = 2')
  self.assertIn('All x',self.calc('x=x','solve'))
  self.assertEqual(self.calc('x=x+1','solve'),'No solution')
  self.assertEqual(self.calc('1e200*x^2-5e200*x+6e200','solve'),'x = 3 or 2')
 def test_invalid_input_and_domains_fail_without_hanging_or_nonfinite_answers(self):
  for text in ['','1/0','sqrt(-1)','ln(0)','asin(2)','tan(pi/2)','1700!','ncr(5,7)','std(2)','root(-8,2)','unknown(2)','min(1e999,2)','1+','2=2','e=2','E=2','x/x','x^x','x^9','a','sin(x)','(' * 30+'1'+')'*30,'2^'*40+'2','-'*100+'2','1'*161]:
   with self.subTest(text=text):self.assertEqual(self.commands(('eval',text))[0][0],-1)
 def test_symbolic_output_and_parser_depth_fail_explicitly_instead_of_truncating(self):
  code,text=self.commands(('expand','(0.271875414384e-17*x+0.257660620525e-11)^8'))[0]
  self.assertEqual(code,-1);self.assertIn('exceeds display limit',text)
  self.assertEqual(self.calc('('*7+'2'+')'*7),'2')
  self.assertEqual(self.commands(('eval','('*8+'2'+')'*8))[0][0],-1)

 def test_numeric_arithmetic_against_independent_reference(self):
  rng=random.Random(71)
  commands=[];expected=[]
  for _ in range(100):
   a,b,c=[rng.randint(-100,100) for _ in range(3)];c=c or 1
   commands.append(('eval',f'({a}+{b})/{c}'));expected.append((a+b)/c)
  for (status,text),number in zip(self.commands(*commands),expected):
   self.assertEqual(status,0,text);self.assertAlmostEqual(float(text.split(' = ')[-1]),number,delta=1e-9)

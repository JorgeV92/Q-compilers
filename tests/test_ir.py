"""Run from the repository root: make test (or python3 tests/test_ir.py build/qlang)."""

import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile


compiler = str(Path(sys.argv[1]).resolve())


def compile_q(source, *flags):
    return subprocess.run(
        [compiler, *flags], input=source, text=True, capture_output=True, check=False
    )


def reject(source, message):
    result = compile_q(source, "--emit-llvm")
    assert result.returncode == 1, result
    assert result.stdout == "", result.stdout
    assert re.search(r"\d+:\d+: error:", result.stderr), result.stderr
    assert message in result.stderr, result.stderr


example = Path(__file__).resolve().parents[1] / "examples" / "scalars.q"
calls_example = example.with_name("calls.q")
foreign_example = example.with_name("foreign.q")
source = example.read_text() + calls_example.read_text() + foreign_example.read_text() + """
fn negate(x) => -x;
fn positive(x) => +x;
fn constant() => .5 + 2e1;
fn subtract(a, b, c) => a - b - c;
fn divide(a, b, c) => a / b / c;
fn __q_expr_0() => 42;
fn nested(x, y) => loss(square(x), square(y));
fn zero_arg_call() => constant();
fn ordered(a, b, c) => subtract(a + 1, b * 2, c / 2) + a;
// Verify self-reference in IR without executing unbounded recursion.
fn recursive(x) => recursive(x);
foreign shifted(original);
fn use_shifted(x) => shifted(x);
foreign shifted(renamed);
fn shifted(value) => value + 1;
foreign shifted(after_definition);
foreign host_affine(x, weight, bias);
fn use_host(x) => host_affine(x, 2, 3);
foreign host_constant();
fn use_host_constant() => host_constant();
2 + 3 * 4;
"""
result = compile_q(source, "--emit-llvm")
assert result.returncode == 0, result.stderr
assert result.stderr == "", result.stderr
assert "@__q_expr.0()" in result.stdout
assert "@__q_expr.1()" in result.stdout
assert result.stdout.count("declare double @sqrt(") == 1
assert result.stdout.count("define double @shifted(") == 1
assert "declare double @shifted(" not in result.stdout

with tempfile.TemporaryDirectory(prefix="q-ir-test-") as directory:
    work = Path(directory)
    ir = work / "scalars.ll"
    ir.write_text(result.stdout)
    subprocess.run(
        [os.environ.get("LLVM_AS", "llvm-as"), str(ir), "-o", str(work / "scalars.bc")],
        check=True,
    )
    caller = work / "caller.c"
    caller.write_text("""
#include <assert.h>
#include <math.h>
extern double linear(double, double, double);
extern double squared_error(double, double);
extern double mean2(double, double);
extern double negate(double);
extern double positive(double);
extern double constant(void);
extern double subtract(double, double, double);
extern double divide(double, double, double);
extern double __q_expr_0(void);
extern double loss(double, double);
extern double nested(double, double);
extern double zero_arg_call(void);
extern double ordered(double, double, double);
extern double norm2(double, double);
extern double sigmoid(double);
extern double use_shifted(double);
extern double use_host(double);
extern double use_host_constant(void);
double host_affine(double x, double weight, double bias) { return x * weight + bias; }
double host_constant(void) { return 7; }
int main(void) {
  assert(linear(3, 2, 1) == 7);
  assert(squared_error(7, 4) == 9);
  assert(mean2(2, 3) == 2.5);
  assert(negate(3) == -3);
  assert(signbit(negate(0.0)));
  assert(positive(-3) == -3);
  assert(constant() == 20.5);
  assert(subtract(10, 3, 2) == 5);
  assert(divide(20, 2, 5) == 2);
  assert(__q_expr_0() == 42);
  assert(loss(7, 4) == 9);
  assert(nested(3, 2) == 25);
  assert(zero_arg_call() == 20.5);
  assert(ordered(10, 3, 2) == 14);
  assert(norm2(3, 4) == 5);
  assert(sigmoid(0) == 0.5);
  assert(fabs(sigmoid(-2) - 0.11920292202211755) < 1e-12);
  assert(use_shifted(9) == 10);
  assert(use_host(4) == 11);
  assert(use_host_constant() == 7);
  return 0;
}
""")
    executable = work / "check"
    subprocess.run(
        [os.environ.get("CLANG", "clang"), str(ir), str(caller), "-lm", "-o", str(executable)],
        check=True,
    )
    subprocess.run([str(executable)], check=True)
    from_file = subprocess.run(
        [compiler, "--emit-llvm", str(example)], text=True, capture_output=True, check=True
    )
    assert from_file.stdout == compile_q(example.read_text(), "--emit-llvm").stdout

reject("fn good(x) => x;\nfn bad(y) => x;", "2:14: error: unknown name 'x'")
reject("fn f(x) => x; fn f(y) => y;", "duplicate function 'f'")
reject("fn f(x, x) => x;", "duplicate parameter")
reject("fn f(x) => x", "expected ';'")
reject("1 + ;", "expected an expression")
reject("fn good(x) => x;\nmissing(1);", "2:1: error: unknown function 'missing'")
reject("fn first(x) => later(x); fn later(x) => x;", "unknown function 'later'")
reject("fn f(x) => x; f();", "incorrect argument count for 'f': expected 1, got 0")
reject("fn f(x) => x; f(1, 2);", "incorrect argument count for 'f': expected 1, got 2")
reject("fn f() => 1; f(2);", "incorrect argument count for 'f': expected 0, got 1")
reject("fn f(x) => x; f(missing);", "unknown name 'missing'")
reject("fn f(x) => x; f(missing());", "unknown function 'missing'")
reject("fn f(x) => x; f(f());", "incorrect argument count for 'f': expected 1, got 0")
for expression in ["!1", "2 ^ 3", "5 % 2", "1 < 2", "1 <= 2", "1 > 2",
                   "1 >= 2", "1 == 2", "1 != 2", "1 |> f"]:
    reject("fn good(x) => x; " + expression + ";", "not supported in this increment")
reject("foreign f(x);\nforeign f(x, y);", "2:9: error: conflicting signature for 'f'")
reject("foreign f(x); fn f() => 1;", "conflicting signature for 'f': expected 1, got 0")
reject("foreign f(); fn f(x) => x;", "conflicting signature for 'f': expected 0, got 1")
reject("fn f(x) => x; foreign f();", "conflicting signature for 'f'")
reject("foreign f(x); fn f(y) => y; fn f(z) => z;", "duplicate function 'f'")
reject("foreign f(old); fn f(value) => old;", "unknown name 'old'")
reject("foreign f(x); f();", "incorrect argument count for 'f': expected 1, got 0")
reject("foreign f(x); f(1, 2);", "incorrect argument count for 'f': expected 1, got 2")
reject("f(1); foreign f(x);", "unknown function 'f'")

declarations = compile_q("foreign f(a); foreign f(b);", "--emit-llvm")
assert declarations.returncode == 0, declarations.stderr
assert declarations.stdout.count("declare double @f(") == 1

ast_source = "foreign f(x); fn square(x) => x ^ 2; square(3) |> f;"
default = compile_q(ast_source)
explicit = compile_q(ast_source, "--dump-ast")
assert default.returncode == explicit.returncode == 0
assert default.stdout == explicit.stdout
assert default.stdout.startswith("Program\n") and "Pipeline" in default.stdout
assert compile_q("", "--emit-llvm").returncode == 0
assert compile_q("", "--unknown").returncode == 2
print("Scalar, call, and foreign IR checks passed: assembly, native execution, diagnostics, and AST mode.")

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
source = example.read_text() + calls_example.read_text() + """
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
2 + 3 * 4;
"""
result = compile_q(source, "--emit-llvm")
assert result.returncode == 0, result.stderr
assert result.stderr == "", result.stderr
assert "@__q_expr.0()" in result.stdout
assert "@__q_expr.1()" in result.stdout

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
  return 0;
}
""")
    executable = work / "check"
    subprocess.run(
        [os.environ.get("CLANG", "clang"), str(ir), str(caller), "-o", str(executable)],
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
reject("foreign f(x);", "not supported in this increment")

ast_source = "foreign f(x); fn square(x) => x ^ 2; square(3) |> f;"
default = compile_q(ast_source)
explicit = compile_q(ast_source, "--dump-ast")
assert default.returncode == explicit.returncode == 0
assert default.stdout == explicit.stdout
assert default.stdout.startswith("Program\n") and "Pipeline" in default.stdout
assert compile_q("", "--emit-llvm").returncode == 0
assert compile_q("", "--unknown").returncode == 2
print("Scalar and call IR checks passed: assembly, native execution, diagnostics, and AST mode.")

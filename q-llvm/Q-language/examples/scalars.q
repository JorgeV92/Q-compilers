// Scalar building blocks for a model and a loss, using LLVM increment 1.
fn linear(x, weight, bias) => x * weight + bias;
fn squared_error(prediction, target) => (prediction - target) * (prediction - target);
fn mean2(a, b) => (a + b) / 2;

// Emitted as __q_expr.0; the compiler does not execute it.
5 / 2;

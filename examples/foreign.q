// These functions are provided by the native math library when linked.
foreign sqrt(value);
foreign exp(value);

fn norm2(x, y) => sqrt(x * x + y * y);
fn sigmoid(x) => 1 / (1 + exp(-x));

// Emitted as a wrapper; qlang does not execute it.
norm2(3, 4);

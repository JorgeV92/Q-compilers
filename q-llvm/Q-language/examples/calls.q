// Define helpers before the functions that use them.
fn square(x) => x * x;
fn loss(prediction, target) => square(prediction - target);

// This call is emitted inside a top-level wrapper, not executed by the compiler.
loss(7, 4);

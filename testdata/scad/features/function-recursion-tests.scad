function f() = f() + f();
function g() = g(g());

module m(x) {
	cube(x);
}

echo(f());

m(g());

translate([20, 20, 0]) m(10);
